#include "tile_map_play_projection.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t actor_limit = 256, baseline_limit = 16;
constexpr std::size_t retained_limit = 64 * 1024 * 1024;
void require(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
} // namespace
void TileMapPlaySource::owner() const {
    require(owner_ == std::this_thread::get_id(),
            "Tile Play projection requires its transport owner thread");
}
void TileMapPlaySource::clear() {
    owner();
    observed_.clear();
    baselines_.clear();
    transfer_.reset();
    world_ = last_ack_ = 0;
    scene_ = {};
}
Json TileMapPlaySource::capture(std::uint64_t world, AssetId scene,
                                std::span<const TileMapPlayView> views) {
    owner();
    require(world && scene && views.size() <= actor_limit,
            "Tile Play projection scope exceeds its profile");
    std::map<EntityId, TileMapPlayView> candidate;
    Json manifest = Json::array();
    for (const auto& view : views) {
        require(view.entity && view.requested && candidate.emplace(view.entity, view).second,
                "Tile Play projection repeats an actor or has an empty identity");
        require(view.diagnostic.size() <= 1024 && view.map_revision.size() <= 256 &&
                    view.set_revision.size() <= 256,
                "Tile Play projection diagnostic/revision exceeds its profile");
        const bool ready = bool(view.cells && view.definitions);
        if (ready)
            require(view.generation && !view.map_revision.empty() && !view.set_revision.empty() &&
                        view.cells->asset() == view.requested &&
                        view.cells->tile_set() == view.definitions->asset(),
                    "Tile Play projection observation has incompatible roots/generation");
        else
            require(!view.cells && !view.definitions, "Tile Play projection has a partial value");
        Json row{{"entity", view.entity},
                 {"map", view.requested},
                 {"ready", ready},
                 {"pending", view.pending},
                 {"diagnostic", view.diagnostic}};
        if (ready) {
            row["generation"] = view.generation;
            row["revision"] = view.revision;
            row["map_revision"] = view.map_revision;
            row["set_revision"] = view.set_revision;
            row["tile_set"] = view.definitions->asset();
            row["edited"] = view.edited;
            row["cells"] = view.cells->occupied();
            row["chunks"] = view.cells->chunk_count();
        }
        manifest.push_back(std::move(row));
    }
    if (world_ != world || scene_ != scene)
        clear();
    world_ = world;
    scene_ = scene;
    observed_ = std::move(candidate);
    // Retire departed actors promptly. A frozen in-flight value remains detached
    // and can finish; the next manifest prevents applying it to a removed actor.
    std::erase_if(baselines_, [&](const auto& entry) { return !observed_.contains(entry.first); });
    return {{"world", world_}, {"scene", scene_}, {"maps", std::move(manifest)}};
}
std::size_t TileMapPlaySource::retained_bytes() const {
    owner();
    std::vector<const TileMapData*> maps;
    std::set<const Value*> values;
    for (const auto& [entity, baseline] : baselines_) {
        (void)entity;
        values.insert(baseline.value.get());
    }
    if (transfer_)
        values.insert(transfer_->value.get());
    std::size_t bytes = 0;
    for (const auto* value : values) {
        maps.push_back(&value->cells);
        bytes += sizeof(Value) + value->definitions.resident_bytes();
        bytes += value->map_revision.capacity() + value->set_revision.capacity() + 2;
    }
    bytes += TileMapData::resident_bytes(maps);
    return bytes;
}
void TileMapPlaySource::trim() {
    while (!baselines_.empty() &&
           (baselines_.size() > baseline_limit || retained_bytes() > retained_limit)) {
        const auto oldest = std::min_element(
            baselines_.begin(), baselines_.end(),
            [](const auto& a, const auto& b) { return a.second.age < b.second.age; });
        baselines_.erase(oldest);
    }
    require(retained_bytes() <= retained_limit,
            "Tile Play transfer exceeds its retained-value budget");
}
Json TileMapPlaySource::begin(EntityId entity, std::uint64_t generation, std::uint64_t revision,
                              std::uint64_t base_generation, std::uint64_t base_revision) {
    owner();
    require(!transfer_, "Finish or cancel the current Tile Play transfer");
    const auto observed = observed_.find(entity);
    if (observed == observed_.end() || !observed->second.cells ||
        observed->second.generation != generation || observed->second.revision != revision)
        return {{"stale", true}};
    require(sequence_ != UINT64_MAX, "Tile Play transfer token space exhausted");
    const auto& view = observed->second;
    // Copy only pure values into retained transport history. Asset/provider
    // leases in the observation must not be pinned by acknowledged baselines.
    auto value =
        std::make_shared<const Value>(Value{*view.cells, *view.definitions, generation, revision,
                                            view.map_revision, view.set_revision});
    const auto baseline = baselines_.find(entity);
    const TileMapData* previous = nullptr;
    if (base_generation && base_generation == generation) {
        if (baseline != baselines_.end() && baseline->second.value->generation == base_generation &&
            baseline->second.value->revision == base_revision)
            previous = &baseline->second.value->cells;
        else if (!base_revision) {
            // An acknowledged unedited asset is also the initial value of other
            // actors with the same adopted Map and Set revisions.
            for (const auto& [other, known] : baselines_) {
                (void)other;
                const auto& original = *known.value;
                if (!original.revision && original.cells.asset() == value->cells.asset() &&
                    original.cells.tile_set() == value->cells.tile_set() &&
                    original.map_revision == value->map_revision &&
                    original.set_revision == value->set_revision) {
                    previous = &original.cells;
                    break;
                }
            }
        }
    }
    const bool full = !previous;
    Transfer transfer{entity, value, tile_map_projection_changes(value->cells, previous),
                      sequence_ + 1, 0};
    Json result{{"stale", false},
                {"token", transfer.token},
                {"full", full},
                {"count", transfer.chunks.size()},
                {"cells", value->cells.occupied()},
                {"chunks", value->cells.chunk_count()}};
    if (full) {
        result["header"] = tile_map_projection_header(value->cells, value->definitions);
        result["keys"] = Json::array();
        for (const auto key : transfer.chunks)
            result["keys"].push_back({key.layer, key.address.x, key.address.y});
    }
    transfer_ = std::move(transfer);
    ++sequence_;
    try {
        trim();
    } catch (...) {
        transfer_.reset();
        throw;
    }
    return result;
}
Json TileMapPlaySource::page(std::uint64_t token, std::size_t cursor) {
    owner();
    require(transfer_ && transfer_->token == token && transfer_->cursor == cursor &&
                cursor < transfer_->chunks.size(),
            "Tile Play page has a stale token or cursor");
    const auto count = std::min(tile_projection_page_chunks, transfer_->chunks.size() - cursor);
    auto data = tile_map_projection_page(transfer_->value->cells,
                                         std::span(transfer_->chunks).subspan(cursor, count));
    Json result{
        {"token", token}, {"cursor", cursor}, {"next", cursor + count}, {"data", std::move(data)}};
    transfer_->cursor += count;
    return result;
}
bool TileMapPlaySource::acknowledge(std::uint64_t token) {
    owner();
    if (token && token == last_ack_)
        return true;
    if (!transfer_)
        return false;
    if (transfer_->token != token || transfer_->cursor != transfer_->chunks.size())
        return false;
    // Strong publication: allocating a new baseline can fail without losing the
    // frozen transfer or accepting an acknowledgement that was not retained.
    baselines_.insert_or_assign(transfer_->entity, Baseline{transfer_->value, transfer_->token});
    last_ack_ = token;
    transfer_.reset();
    trim();
    return true;
}
bool TileMapPlaySource::cancel(std::uint64_t token) {
    owner();
    if (!transfer_ || transfer_->token != token)
        return false;
    transfer_.reset();
    return true;
}
} // namespace forge
