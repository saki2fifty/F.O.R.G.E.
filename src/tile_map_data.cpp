#include "tile_map_data.hpp"
#include "cooked_envelope.hpp"
#include "tile_set_data.hpp"
#include <algorithm>
#include <bit>
#include <optional>
#include <set>

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
std::uint64_t natural(const Json& j, std::uint64_t maximum) {
    require(j.is_number_unsigned() || (j.is_number_integer() && j.get<std::int64_t>() >= 0),
            "Cooked Tile Map requires exact unsigned integers");
    const auto value = j.get<std::uint64_t>();
    require(value <= maximum, "Cooked Tile Map unsigned integer is outside its range");
    return value;
}
std::uint64_t read64(std::span<const std::byte> bytes, std::size_t& at) {
    const auto low = asset_detail::cooked_read32(bytes, at);
    const auto high = asset_detail::cooked_read32(bytes, at);
    return low | (std::uint64_t(high) << 32);
}
std::int32_t order(const Json& j) {
    require(j.is_number_integer(), "Cooked Tile Map order must be an integer");
    if (j.is_number_unsigned())
        return std::int32_t(natural(j, INT32_MAX));
    const auto value = j.get<std::int64_t>();
    require(value >= INT32_MIN && value <= INT32_MAX, "Cooked Tile Map order outside int32");
    return std::int32_t(value);
}
} // namespace
TileMapData decode_cooked_tile_map(std::span<const std::byte> bytes, AssetId expected) {
    const auto envelope =
        asset_detail::decode_envelope(bytes, tile_map_cooked_magic, tile_map_cooked_metadata_limit,
                                      tile_map_cooked_payload_limit);
    const auto& root = envelope.metadata;
    require(root.is_object() && root.size() == 6, "Invalid cooked Tile Map metadata fields");
    TileMapData value;
    value.asset_ = root.at("asset_id").get<AssetId>();
    value.tile_set_ = root.at("tile_set").get<AssetId>();
    require(bool(expected) && value.asset_ == expected && bool(value.tile_set_),
            "Cooked Tile Map identity/reference mismatch");
    const auto& size = root.at("cell_size");
    require(size.is_array() && size.size() == 2, "Invalid cooked Tile Map cell size");
    for (unsigned axis = 0; axis < 2; ++axis) {
        require(size[axis].is_number(), "Cooked Tile Map cell size must be numeric");
        const auto n = size[axis].get<double>();
        require(std::isfinite(n) && n >= 1e-6 && n <= 1e6, "Invalid cooked Tile Map meter range");
        value.cell_size_[axis] = n;
    }
    const auto& layers = root.at("layers");
    require(layers.is_array() && !layers.empty() && layers.size() <= tile_map_layer_limit,
            "Cooked Tile Map layer count exceeds its profile");
    std::set<std::uint64_t> ids;
    for (const auto& row : layers) {
        require(row.is_object() && row.size() == 4, "Invalid cooked Tile Map layer fields");
        TileMapLayer layer;
        layer.id = natural(row.at("id"), UINT64_MAX - 1);
        require(layer.id && ids.insert(layer.id).second, "Invalid/duplicate cooked map layer ID");
        layer.name = row.at("name").get<std::string>();
        require(!layer.name.empty() && layer.name.size() <= 128 &&
                    std::none_of(layer.name.begin(), layer.name.end(),
                                 [](unsigned char c) { return c < 32 || c == 127; }),
                "Invalid cooked Tile Map layer name");
        require(row.at("visible").is_boolean(), "Invalid cooked Tile Map layer visibility");
        layer.visible = row.at("visible").get<bool>();
        layer.order = order(row.at("order"));
        value.layers_.push_back(std::move(layer));
    }
    const auto chunks = natural(root.at("chunk_count"), tile_map_chunk_limit);
    const auto count = natural(root.at("cell_count"), tile_map_cell_limit);
    require(count >= chunks && count <= chunks * tile_chunk_cells &&
                envelope.payload.size() == chunks * 20 + count * 16,
            "Cooked Tile Map counts/payload length disagree");
    std::size_t at = 0;
    std::optional<TileMapChunkKey> previous;
    for (std::uint64_t i = 0; i < chunks; ++i) {
        TileMapChunkKey key;
        key.layer = read64(envelope.payload, at);
        key.address.x =
            std::bit_cast<std::int32_t>(asset_detail::cooked_read32(envelope.payload, at));
        key.address.y =
            std::bit_cast<std::int32_t>(asset_detail::cooked_read32(envelope.payload, at));
        require(ids.contains(key.layer) && (!previous || *previous < key),
                "Cooked Tile Map chunk layer/order/uniqueness mismatch");
        previous = key;
        (void)tile_cell_address({key.address, 0});
        (void)tile_cell_address({key.address, tile_chunk_cells - 1});
        const auto occupied = asset_detail::cooked_read32(envelope.payload, at);
        require(occupied && occupied <= tile_chunk_cells && occupied <= count - value.occupied_,
                "Cooked Tile Map chunk occupancy mismatch");
        auto chunk = std::make_shared<TileMapData::Chunk>();
        chunk->occupied = occupied;
        std::optional<std::uint32_t> last_index;
        for (std::uint32_t n = 0; n < occupied; ++n) {
            const auto index = asset_detail::cooked_read32(envelope.payload, at);
            require(index < tile_chunk_cells && (!last_index || *last_index < index),
                    "Cooked Tile Map cell index/order/uniqueness mismatch");
            last_index = index;
            TileMapCell cell;
            cell.tile = read64(envelope.payload, at);
            cell.flags = asset_detail::cooked_read32(envelope.payload, at);
            require(cell.tile && cell.flags <= 7, "Cooked Tile Map cell value is invalid");
            chunk->cells[index] = cell;
        }
        value.occupied_ += occupied;
        value.chunks_.emplace(key, std::move(chunk));
    }
    require(at == envelope.payload.size() && value.occupied_ == count,
            "Cooked Tile Map cell total/trailing bytes mismatch");
    value.admitted_ = true;
    value.validate();
    return value;
}
void TileMapData::validate() const {
    require(admitted_ && bool(asset_) && bool(tile_set_) && !layers_.empty() &&
                layers_.size() <= tile_map_layer_limit && chunks_.size() <= tile_map_chunk_limit &&
                occupied_ >= chunks_.size() && occupied_ <= tile_map_cell_limit &&
                occupied_ <= chunks_.size() * tile_chunk_cells,
            "Cooked Tile Map value was not admitted or was moved from");
}
AssetId TileMapData::asset() const {
    validate();
    return asset_;
}
AssetId TileMapData::tile_set() const {
    validate();
    return tile_set_;
}
std::array<double, 2> TileMapData::cell_size() const {
    validate();
    return cell_size_;
}
const std::vector<TileMapLayer>& TileMapData::layers() const {
    validate();
    return layers_;
}
std::size_t TileMapData::occupied() const {
    validate();
    return occupied_;
}
std::size_t TileMapData::chunk_count() const {
    validate();
    return chunks_.size();
}
bool TileMapData::has_layer(std::uint64_t layer) const {
    return std::any_of(layers_.begin(), layers_.end(),
                       [&](const auto& row) { return row.id == layer; });
}
TileMapCell TileMapData::cell(std::uint64_t layer, TileCellAddress cell) const {
    validate();
    require(has_layer(layer), "Cooked Tile Map query has no matching layer");
    const auto address = tile_chunk_cell(cell);
    const auto found = chunks_.find({layer, address.chunk});
    return found == chunks_.end() ? TileMapCell{} : found->second->cells[address.index];
}
TileMapData::CellChanges TileMapData::prepare_cells(std::span<const TileMapCellEdit> writes,
                                                    const TileSetData* definitions) const {
    validate();
    if (definitions)
        require(definitions->asset() == tile_set_,
                "Runtime Tile Map definitions have a different root");
    require(writes.size() <= tile_map_edit_limit, "Runtime Tile Map edit batch exceeds its limit");
    CellChanges changes;
    auto& patches = changes.chunks;
    for (const auto& write : writes) {
        require(has_layer(write.layer), "Runtime Tile Map edit has no matching layer");
        require(write.value.flags <= 7 && (write.value.tile || !write.value.flags),
                "Runtime Tile Map edit has invalid cell flags");
        if (write.value.tile) {
            require(definitions, "Tile Map erase batch contains a nonempty cell");
            (void)definitions->tile(write.value.tile);
        }
        const auto address = tile_chunk_cell(write.address);
        patches[{write.layer, address.chunk}].cells[address.index] = write.value;
    }
    // Count the final state before allocating chunk arrays. This permits moves
    // at capacity and bounds scratch work by the copied edit batch, rather than
    // allocating a full chunk for each arbitrary input address before rejection.
    auto occupied = occupied_;
    auto chunks = chunks_.size();
    for (auto& [key, patch] : patches) {
        const auto old = chunks_.find(key);
        const auto count = old == chunks_.end() ? 0 : old->second->occupied;
        patch.occupied = count;
        for (const auto& [index, cell] : patch.cells) {
            if (old != chunks_.end() && old->second->cells[index].tile)
                --patch.occupied;
            if (cell.tile)
                ++patch.occupied;
        }
        occupied = occupied - count + patch.occupied;
        if (old != chunks_.end() && !patch.occupied)
            --chunks;
        else if (old == chunks_.end() && patch.occupied)
            ++chunks;
    }
    require(occupied <= tile_map_cell_limit && chunks <= tile_map_chunk_limit,
            "Runtime Tile Map edit exceeds its cell/chunk profile");
    changes.occupied = occupied;
    return changes;
}
void TileMapData::validate_cells(std::span<const TileMapCellEdit> writes,
                                 const TileSetData& definitions) const {
    (void)prepare_cells(writes, &definitions);
}
TileMapData TileMapData::with_cells(std::span<const TileMapCellEdit> writes,
                                    const TileSetData& definitions) const {
    return apply_cells(prepare_cells(writes, &definitions));
}
TileMapData TileMapData::with_erased_cells(std::span<const TileMapCellEdit> writes) const {
    return apply_cells(prepare_cells(writes, nullptr));
}
TileMapData TileMapData::apply_cells(const CellChanges& changes) const {
    auto next = *this;
    for (const auto& [key, patch] : changes.chunks) {
        const auto old = chunks_.find(key);
        if (!patch.occupied) {
            next.chunks_.erase(key);
            continue;
        }
        if (old != chunks_.end() &&
            std::all_of(patch.cells.begin(), patch.cells.end(), [&](const auto& cell) {
                return old->second->cells[cell.first] == cell.second;
            }))
            continue;
        auto chunk = old == chunks_.end() ? std::make_shared<Chunk>()
                                          : std::make_shared<Chunk>(*old->second);
        for (const auto& [index, cell] : patch.cells)
            chunk->cells[index] = cell;
        chunk->occupied = patch.occupied;
        next.chunks_[key] = std::move(chunk);
    }
    next.occupied_ = changes.occupied;
    next.validate();
    return next;
}
void TileMapData::for_each_chunk(
    const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                             std::size_t)>& visit) const {
    validate();
    require(bool(visit), "Cooked Tile Map chunk visitor is empty");
    for (const auto& [key, chunk] : chunks_)
        visit(key, chunk->cells, chunk->occupied);
}
std::size_t TileMapData::resident_bytes() const {
    validate();
    std::size_t bytes = sizeof(*this) + layers_.capacity() * sizeof(TileMapLayer);
    for (const auto& layer : layers_)
        bytes += layer.name.capacity() + 1;
    return bytes + chunks_.size() * (sizeof(Chunk) + sizeof(TileMapChunkKey) +
                                     sizeof(std::shared_ptr<const Chunk>) + 64);
}
std::size_t TileMapData::resident_bytes(std::span<const TileMapData* const> views) {
    std::set<const TileMapData*> values;
    std::set<const Chunk*> arrays;
    std::size_t bytes = 0;
    for (const auto* value : views) {
        require(value, "Tile Map memory estimate received a null view");
        if (!values.insert(value).second)
            continue;
        bytes += value->resident_bytes() - value->chunks_.size() * sizeof(Chunk);
        for (const auto& [key, chunk] : value->chunks_) {
            (void)key;
            if (arrays.insert(chunk.get()).second)
                bytes += sizeof(Chunk);
        }
    }
    return bytes;
}
} // namespace forge
