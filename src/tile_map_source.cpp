#include "tile_map_source.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <charconv>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
std::uint64_t natural(const Json& j, std::uint64_t maximum = UINT64_MAX) {
    require(j.is_number_unsigned() || (j.is_number_integer() && j.get<std::int64_t>() >= 0),
            "Tile Map value must be an unsigned integer");
    const auto n = j.get<std::uint64_t>();
    require(n <= maximum, "Tile Map unsigned value is outside its range");
    return n;
}
std::int32_t signed_integer(const Json& j) {
    require(j.is_number_integer(), "Tile Map value must be an integer");
    if (j.is_number_unsigned()) {
        require(j.get<std::uint64_t>() <= INT32_MAX, "Tile Map integer is outside signed range");
        return std::int32_t(j.get<std::uint64_t>());
    }
    const auto n = j.get<std::int64_t>();
    require(n >= INT32_MIN && n <= INT32_MAX, "Tile Map integer is outside signed range");
    return std::int32_t(n);
}
std::string key_string(const TileMapChunkKey& key) {
    return std::to_string(key.layer) + ":" + std::to_string(key.address.x) + ":" +
           std::to_string(key.address.y);
}
TileMapChunkKey parse_key(const std::string& key) {
    const auto a = key.find(':'), b = key.find(':', a == std::string::npos ? a : a + 1);
    require(a != std::string::npos && b != std::string::npos, "Invalid Tile Map chunk key");
    TileMapChunkKey result;
    const auto parse = [&](std::size_t begin, std::size_t end, auto& value) {
        const auto p = std::from_chars(key.data() + begin, key.data() + end, value);
        require(p.ec == std::errc{} && p.ptr == key.data() + end, "Invalid Tile Map chunk key");
    };
    parse(0, a, result.layer);
    parse(a + 1, b, result.address.x);
    parse(b + 1, key.size(), result.address.y);
    require(result.layer && key_string(result) == key, "Noncanonical Tile Map chunk key");
    (void)tile_cell_address({result.address, 0});
    (void)tile_cell_address({result.address, tile_chunk_cells - 1});
    return result;
}
std::size_t structure(const Json& value, int depth) {
    // Match the existing parser's value/start/end/key events. Cache the count in
    // each immutable part so a paint candidate can enforce one whole-source
    // structural budget without scanning or serializing unchanged chunks.
    const auto count = [&](auto&& self, const Json& item, int level) -> std::size_t {
        require(level <= depth, "Tile Map source exceeds JSON depth limit");
        std::size_t events = item.is_structured() ? 2 : 1;
        if (item.is_object())
            events += item.size();
        require(events <= tile_map_source_event_limit, "Tile Map source exceeds JSON event limit");
        if (item.is_number_float())
            require(std::isfinite(item.get<double>()),
                    "Tile Map source has a non-finite JSON value");
        if (item.is_structured())
            for (const auto& child : item) {
                events += self(self, child, level + 1);
                require(events <= tile_map_source_event_limit,
                        "Tile Map source exceeds JSON event limit");
            }
        return events;
    };
    const auto events = count(count, value, 0);
    const auto text = value.dump();
    (void)asset_detail::parse_bounded_json(std::as_bytes(std::span(text)),
                                           tile_map_source_byte_limit, tile_map_source_event_limit,
                                           depth);
    return events;
}
void cell_valid(TileMapCell cell) {
    require(cell.flags <= 7 && (cell.tile || !cell.flags), "Invalid Tile Map cell flags");
}
} // namespace
std::shared_ptr<const TileMapValue::Metadata> TileMapValue::admit_metadata(Json root) {
    require(root.is_object() && !root.contains("chunks") && root.at("format") == "forge.tile-map" &&
                natural(root.at("version")) == 1,
            "Unsupported Tile Map source format/version");
    auto value = std::make_shared<Metadata>();
    value->asset = root.at("asset_id").get<AssetId>();
    value->tile_set = root.at("tile_set").get<AssetId>();
    value->next_id = natural(root.at("next_id"));
    require(bool(value->asset) && bool(value->tile_set) && value->next_id,
            "Tile Map requires asset/Tile Set identities and a positive ID sequence");
    const auto& size = root.at("cell_size");
    require(size.is_array() && size.size() == 2, "Tile Map cell size requires local XY meters");
    for (unsigned axis = 0; axis < 2; ++axis) {
        require(size[axis].is_number(), "Tile Map cell size must be numeric");
        const auto n = size[axis].get<double>();
        require(std::isfinite(n) && n >= 1e-6 && n <= 1e6,
                "Tile Map cell size is outside its finite meter range");
        value->cell_size[axis] = n;
    }
    const auto& layers = root.at("layers");
    require(layers.is_array() && !layers.empty() && layers.size() <= tile_map_layer_limit,
            "Tile Map needs a bounded nonempty layer array");
    std::set<std::uint64_t> ids;
    for (const auto& row : layers) {
        TileMapLayer layer;
        layer.id = natural(row.at("id"));
        require(layer.id && layer.id < value->next_id && ids.insert(layer.id).second,
                "Tile Map layer ID is duplicated or unallocated");
        layer.name = row.at("name").get<std::string>();
        require(!layer.name.empty() && layer.name.size() <= 128 &&
                    std::none_of(layer.name.begin(), layer.name.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "Invalid Tile Map layer name");
        require(row.at("visible").is_boolean(), "Tile Map layer visibility must be boolean");
        layer.visible = row.at("visible").get<bool>();
        layer.order = signed_integer(row.at("order"));
        value->layers.push_back(std::move(layer));
    }
    value->json_events = structure(root, 16);
    value->encoded_bytes = root.dump().size();
    value->document = std::move(root);
    return value;
}
std::shared_ptr<const TileMapValue::Chunk> TileMapValue::admit_chunk(Json row) {
    require(row.is_object() && row.at("cells").is_array() &&
                row.at("cells").size() == tile_chunk_cells,
            "Tile Map chunk must contain exactly 1024 cell slots");
    auto value = std::make_shared<Chunk>();
    for (std::size_t i = 0; i < tile_chunk_cells; ++i) {
        const auto& cell = row.at("cells")[i];
        if (cell.is_null())
            continue;
        require(cell.is_object(), "Tile Map cell must be an object or null");
        TileMapCell decoded{natural(cell.at("tile")), std::uint32_t(natural(cell.at("flags"), 7))};
        require(decoded.tile != 0, "An empty Tile Map source cell must be null");
        value->cells[i] = decoded;
        ++value->occupied;
    }
    value->json_events = structure(row, 12);
    value->encoded_bytes = row.dump().size();
    value->document = std::move(row);
    return value;
}
TileMapValue TileMapValue::create(AssetId id, AssetId tile_set) {
    return parse(
        {{"format", "forge.tile-map"},
         {"version", 1},
         {"asset_id", id},
         {"tile_set", tile_set},
         {"next_id", 2},
         {"cell_size", {1., 1.}},
         {"layers",
          Json::array({{{"id", 1}, {"name", "Layer 1"}, {"visible", true}, {"order", 0}}})},
         {"chunks", Json::object()}});
}
TileMapValue TileMapValue::parse(Json root) {
    require(root.is_object() && root.at("chunks").is_object() &&
                root.at("chunks").size() <= tile_map_chunk_limit,
            "Tile Map chunk directory exceeds its profile");
    auto chunks = std::move(root.at("chunks"));
    root.erase("chunks");
    TileMapValue value;
    value.metadata_ = admit_metadata(std::move(root));
    for (auto& [name, row] : chunks.items()) {
        const auto key = parse_key(name);
        require(value.has_layer(key.layer), "Tile Map chunk refers to a missing layer");
        auto chunk = admit_chunk(std::move(row));
        value.occupied_ += chunk->occupied;
        value.encoded_bytes_ += chunk->encoded_bytes + name.size() + 4;
        value.json_events_ += chunk->json_events + 1; // The chunk directory key.
        value.chunks_.emplace(key, std::move(chunk));
        value.validate();
    }
    value.validate();
    return value;
}
AssetId TileMapValue::asset() const {
    validate();
    return metadata_->asset;
}
AssetId TileMapValue::tile_set() const {
    validate();
    return metadata_->tile_set;
}
std::uint64_t TileMapValue::next_id() const {
    validate();
    return metadata_->next_id;
}
std::array<double, 2> TileMapValue::cell_size() const {
    validate();
    return metadata_->cell_size;
}
const std::vector<TileMapLayer>& TileMapValue::layers() const {
    validate();
    return metadata_->layers;
}
std::size_t TileMapValue::occupied() const {
    validate();
    return occupied_;
}
std::size_t TileMapValue::chunk_count() const {
    validate();
    return chunks_.size();
}
const Json& TileMapValue::metadata() const {
    validate();
    return metadata_->document;
}
void TileMapValue::for_each_chunk(
    const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                             std::size_t)>& visit) const {
    validate();
    require(bool(visit), "Tile Map chunk visitor is empty");
    for (const auto& [key, chunk] : chunks_)
        visit(key, chunk->cells, chunk->occupied);
}
bool TileMapValue::has_layer(std::uint64_t id) const {
    return metadata_ && std::any_of(metadata_->layers.begin(), metadata_->layers.end(),
                                    [&](const auto& layer) { return layer.id == id; });
}
void TileMapValue::validate() const {
    require(bool(metadata_) && occupied_ <= tile_map_cell_limit &&
                chunks_.size() <= tile_map_chunk_limit &&
                encoded_bytes_ + metadata_->encoded_bytes + 64 <= tile_map_source_byte_limit &&
                json_events_ + metadata_->json_events + 3 <= tile_map_source_event_limit,
            "Tile Map immutable value is uninitialized or exceeds its source profile");
}
TileMapCell TileMapValue::cell(std::uint64_t layer, TileCellAddress cell) const {
    validate();
    require(has_layer(layer), "Tile Map cell query refers to a missing layer");
    const auto part = tile_chunk_cell(cell);
    const auto found = chunks_.find({layer, part.chunk});
    return found == chunks_.end() ? TileMapCell{} : found->second->cells[part.index];
}
TileMapValue TileMapValue::paint(std::span<const TileMapCellEdit> writes) const {
    validate();
    require(writes.size() <= tile_map_edit_limit, "Tile Map paint batch exceeds its limit");
    for (const auto& write : writes) {
        require(has_layer(write.layer), "Tile Map paint refers to a missing layer");
        cell_valid(write.value);
    }
    std::map<TileMapChunkKey, Json> changed;
    for (const auto& write : writes) {
        const auto part = tile_chunk_cell(write.address);
        const TileMapChunkKey key{write.layer, part.chunk};
        auto found = changed.find(key);
        if (found == changed.end()) {
            const auto old = chunks_.find(key);
            found = changed
                        .emplace(key, old == chunks_.end()
                                          ? Json{{"cells", std::vector<Json>(tile_chunk_cells)}}
                                          : old->second->document)
                        .first;
        }
        auto& cell = found->second.at("cells")[part.index];
        if (!write.value.tile)
            cell = nullptr;
        else {
            if (cell.is_null())
                cell = Json::object();
            cell["tile"] = write.value.tile;
            cell["flags"] = write.value.flags;
        }
    }
    auto next = *this;
    for (auto& [key, row] : changed) {
        const auto old = chunks_.find(key);
        if (old != chunks_.end() && old->second->document == row)
            continue;
        auto chunk = admit_chunk(std::move(row));
        const auto overhead = key_string(key).size() + 4;
        if (old != chunks_.end()) {
            next.occupied_ -= old->second->occupied;
            next.encoded_bytes_ -= old->second->encoded_bytes + overhead;
            next.json_events_ -= old->second->json_events + 1;
        }
        if (!chunk->occupied && chunk->document.size() == 1) {
            next.chunks_.erase(key);
            continue;
        }
        next.occupied_ += chunk->occupied;
        next.encoded_bytes_ += chunk->encoded_bytes + overhead;
        next.json_events_ += chunk->json_events + 1;
        next.chunks_[key] = std::move(chunk);
    }
    next.validate();
    return next;
}
TileMapValue TileMapValue::with_metadata(Json root) const {
    validate();
    auto next = *this;
    next.metadata_ = admit_metadata(std::move(root));
    require(!occupied_ || next.metadata_->tile_set == metadata_->tile_set,
            "Clear painted cells before changing their Tile Set identity");
    for (const auto& [key, chunk] : chunks_) {
        (void)chunk;
        require(next.has_layer(key.layer),
                "Remove a layer's chunks before removing its definition");
    }
    next.validate();
    return next;
}
std::string TileMapValue::dump(int indent) const {
    validate();
    auto root = metadata_->document;
    root["chunks"] = Json::object();
    for (const auto& [key, chunk] : chunks_)
        root["chunks"][key_string(key)] = chunk->document;
    return root.dump(indent);
}
std::size_t TileMapValue::history_weight(const TileMapValue& from, const TileMapValue& to) {
    from.validate();
    to.validate();
    // Existing 8MiB history policy, weighted by encoded metadata, snapshot
    // references and old chunks replaced by this operation. This is not RSS.
    std::size_t bytes = from.metadata_->encoded_bytes +
                        from.chunks_.size() * (sizeof(TileMapChunkKey) + sizeof(void*) * 4);
    for (const auto& [key, chunk] : from.chunks_) {
        const auto other = to.chunks_.find(key);
        if (other == to.chunks_.end() || other->second != chunk)
            bytes += chunk->encoded_bytes;
    }
    return bytes;
}
bool operator==(const TileMapValue& a, const TileMapValue& b) {
    if (!a.metadata_ || !b.metadata_)
        return a.metadata_ == b.metadata_;
    if (a.occupied_ != b.occupied_ || a.chunks_.size() != b.chunks_.size() ||
        (a.metadata_ != b.metadata_ && a.metadata_->document != b.metadata_->document))
        return false;
    auto i = a.chunks_.begin(), j = b.chunks_.begin();
    for (; i != a.chunks_.end(); ++i, ++j)
        if (i->first != j->first ||
            (i->second != j->second && i->second->document != j->second->document))
            return false;
    return true;
}
TileMapSource TileMapSource::parse(std::span<const std::byte> bytes) {
    return {TileMapValue::parse(asset_detail::parse_bounded_json(bytes, tile_map_source_byte_limit,
                                                                 tile_map_source_event_limit, 16))};
}
} // namespace forge
