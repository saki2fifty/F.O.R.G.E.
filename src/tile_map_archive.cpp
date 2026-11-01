#include "tile_map_archive.hpp"
#include "cooked_envelope.hpp"
#include "tile_map_chunk_codec.hpp"
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
std::int32_t order(const Json& j) {
    require(j.is_number_integer(), "Cooked Tile Map order must be an integer");
    if (j.is_number_unsigned())
        return std::int32_t(natural(j, INT32_MAX));
    const auto value = j.get<std::int64_t>();
    require(value >= INT32_MIN && value <= INT32_MAX, "Cooked Tile Map order outside int32");
    return std::int32_t(value);
}
} // namespace
TileMapArchive::Storage TileMapArchive::index(std::span<const std::byte> bytes, AssetId expected,
                                              TileMapData* complete) {
    const auto envelope =
        asset_detail::decode_envelope(bytes, tile_map_cooked_magic, tile_map_cooked_metadata_limit,
                                      tile_map_cooked_payload_limit);
    const auto& root = envelope.metadata;
    require(root.is_object() && root.size() == 6, "Invalid cooked Tile Map metadata fields");
    Storage value;
    value.asset = root.at("asset_id").get<AssetId>();
    value.tile_set = root.at("tile_set").get<AssetId>();
    require(bool(expected) && value.asset == expected && bool(value.tile_set),
            "Cooked Tile Map identity/reference mismatch");
    const auto& size = root.at("cell_size");
    require(size.is_array() && size.size() == 2, "Invalid cooked Tile Map cell size");
    for (unsigned axis = 0; axis < 2; ++axis) {
        require(size[axis].is_number(), "Cooked Tile Map cell size must be numeric");
        const auto n = size[axis].get<double>();
        require(std::isfinite(n) && n >= 1e-6 && n <= 1e6, "Invalid cooked Tile Map meter range");
        value.cell_size[axis] = n;
    }
    const auto& layers = root.at("layers");
    require(layers.is_array() && !layers.empty() && layers.size() <= tile_map_layer_limit,
            "Cooked Tile Map layer count exceeds its profile");
    value.layers.reserve(layers.size());
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
        value.layers.push_back(std::move(layer));
    }
    const auto chunks = natural(root.at("chunk_count"), tile_map_chunk_limit);
    const auto count = natural(root.at("cell_count"), tile_map_cell_limit);
    require(count >= chunks && count <= chunks * tile_chunk_cells &&
                envelope.payload.size() == chunks * 20 + count * 16,
            "Cooked Tile Map counts/payload length disagree");
    if (!complete)
        value.chunks.reserve(chunks);
    const auto payload_offset = bytes.size() - envelope.payload.size();
    std::size_t at = 0;
    std::optional<TileMapChunkKey> previous;
    for (std::uint64_t i = 0; i < chunks; ++i) {
        TileMapChunkKey key;
        key.layer = tile_detail::read_map_u64(envelope.payload, at);
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
        require(occupied && occupied <= tile_chunk_cells && occupied <= count - value.occupied,
                "Cooked Tile Map chunk occupancy mismatch");
        TileMapArchiveChunk chunk;
        chunk.key = key;
        chunk.occupied = occupied;
        chunk.offset_ = payload_offset + at;
        auto dense = complete ? std::make_shared<TileMapData::Chunk>() : nullptr;
        std::optional<std::uint32_t> last_index;
        for (std::uint32_t n = 0; n < occupied; ++n) {
            const auto index = asset_detail::cooked_read32(envelope.payload, at);
            require(index < tile_chunk_cells && (!last_index || *last_index < index),
                    "Cooked Tile Map cell index/order/uniqueness mismatch");
            last_index = index;
            TileMapCell cell;
            cell.tile = tile_detail::read_map_u64(envelope.payload, at);
            cell.flags = asset_detail::cooked_read32(envelope.payload, at);
            require(cell.tile && cell.flags <= 7, "Cooked Tile Map cell value is invalid");
            if (dense)
                dense->cells[index] = cell;
        }
        value.occupied += occupied;
        if (dense) {
            dense->occupied = occupied;
            complete->chunks_.emplace(key, std::move(dense));
        } else {
            value.chunks.push_back(chunk);
        }
    }
    require(at == envelope.payload.size() && value.occupied == count,
            "Cooked Tile Map cell total/trailing bytes mismatch");
    return value;
}
void TileMapArchive::validate() const {
    require(storage_ && storage_->bytes && storage_->bytes->size() >= 24 &&
                storage_->bytes->size() <=
                    24 + tile_map_cooked_metadata_limit + tile_map_cooked_payload_limit &&
                bool(storage_->asset) && bool(storage_->tile_set) && !storage_->layers.empty() &&
                storage_->layers.size() <= tile_map_layer_limit &&
                storage_->chunks.size() <= tile_map_chunk_limit &&
                storage_->occupied >= storage_->chunks.size() &&
                storage_->occupied <= tile_map_cell_limit &&
                storage_->occupied <= storage_->chunks.size() * tile_chunk_cells,
            "Tile Map archive was not admitted or was moved from");
}
AssetId TileMapArchive::asset() const {
    validate();
    return storage_->asset;
}
AssetId TileMapArchive::tile_set() const {
    validate();
    return storage_->tile_set;
}
std::array<double, 2> TileMapArchive::cell_size() const {
    validate();
    return storage_->cell_size;
}
const std::vector<TileMapLayer>& TileMapArchive::layers() const {
    validate();
    return storage_->layers;
}
std::span<const TileMapArchiveChunk> TileMapArchive::chunks() const {
    validate();
    return storage_->chunks;
}
std::size_t TileMapArchive::occupied() const {
    validate();
    return storage_->occupied;
}
const TileMapArchiveChunk* TileMapArchive::find(TileMapChunkKey key) const {
    const auto found =
        std::lower_bound(storage_->chunks.begin(), storage_->chunks.end(), key,
                         [](const auto& chunk, const auto& wanted) { return chunk.key < wanted; });
    return found == storage_->chunks.end() || found->key != key ? nullptr : &*found;
}
TileMapCell TileMapArchive::cell(std::uint64_t layer, TileCellAddress address) const {
    validate();
    require(std::any_of(storage_->layers.begin(), storage_->layers.end(),
                        [&](const auto& row) { return row.id == layer; }),
            "Tile Map archive query has no matching layer");
    const auto location = tile_chunk_cell(address);
    const auto* chunk = find({layer, location.chunk});
    if (!chunk)
        return {};
    return tile_detail::admitted_chunk_cell(
        std::span<const std::byte>(*storage_->bytes).subspan(chunk->offset_, chunk->occupied * 16),
        location.index);
}
TileMapChunkData TileMapArchive::decode_chunk(std::span<const std::byte> bytes,
                                              const TileMapArchiveChunk& chunk,
                                              std::stop_token stop) {
    return {chunk.key,
            tile_detail::decode_admitted_cells(bytes.subspan(chunk.offset_, chunk.occupied * 16),
                                               chunk.occupied, stop),
            chunk.occupied};
}
TileMapChunkData TileMapArchive::decode_chunk(TileMapChunkKey key, std::stop_token stop) const {
    validate();
    const auto* chunk = find(key);
    require(chunk, "Tile Map archive has no matching chunk");
    return decode_chunk(*storage_->bytes, *chunk, stop);
}
TileMapChunkInput TileMapArchive::chunk_input(TileMapChunkKey key) const {
    validate();
    const auto* chunk = find(key);
    require(chunk, "Tile Map archive has no matching chunk");
    return chunk_input(*chunk);
}
TileMapChunkInput TileMapArchive::chunk_input(const TileMapArchiveChunk& row) const {
    auto chunk = std::make_shared<TileMapData::EncodedChunk>();
    chunk->bytes = storage_->bytes;
    chunk->offset = row.offset_;
    chunk->occupied = row.occupied;
    TileMapChunkInput input;
    input.asset_ = storage_->asset;
    input.key_ = row.key;
    input.storage_ = std::move(chunk);
    return input;
}
TileMapData TileMapArchive::materialize(const Storage& storage, std::span<const std::byte> bytes,
                                        std::stop_token stop) {
    require(!stop.stop_requested(), "Tile Map materialization cancelled");
    TileMapData value;
    value.asset_ = storage.asset;
    value.tile_set_ = storage.tile_set;
    value.cell_size_ = storage.cell_size;
    value.layers_ = storage.layers;
    value.occupied_ = storage.occupied;
    for (const auto& row : storage.chunks) {
        const auto decoded = decode_chunk(bytes, row, stop);
        auto chunk = std::make_shared<TileMapData::Chunk>();
        chunk->cells = decoded.cells;
        chunk->occupied = decoded.occupied;
        value.chunks_.emplace(row.key, std::move(chunk));
    }
    require(!stop.stop_requested(), "Tile Map materialization cancelled");
    value.admitted_ = true;
    value.validate();
    return value;
}
TileMapData TileMapArchive::materialize(std::stop_token stop) const {
    validate();
    return materialize(*storage_, *storage_->bytes, stop);
}
TileMapData TileMapArchive::compact() const {
    validate();
    TileMapData value;
    value.asset_ = storage_->asset;
    value.tile_set_ = storage_->tile_set;
    value.cell_size_ = storage_->cell_size;
    value.layers_ = storage_->layers;
    value.occupied_ = storage_->occupied;
    for (const auto& row : storage_->chunks) {
        auto input = chunk_input(row);
        value.chunks_.emplace(row.key, std::move(input.storage_));
    }
    value.admitted_ = true;
    value.validate();
    return value;
}
std::size_t TileMapArchive::resident_bytes() const {
    validate();
    std::size_t bytes = sizeof(*this) + sizeof(Storage) + 64 +
                        storage_->layers.capacity() * sizeof(TileMapLayer) +
                        storage_->chunks.capacity() * sizeof(TileMapArchiveChunk) +
                        sizeof(std::vector<std::byte>) + 64 + storage_->bytes->capacity();
    for (const auto& layer : storage_->layers)
        bytes += layer.name.capacity() + 1;
    return bytes;
}
TileMapArchive decode_tile_map_archive(std::span<const std::byte> bytes, AssetId expected) {
    auto storage = TileMapArchive::index(bytes, expected);
    storage.bytes = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
    TileMapArchive value;
    value.storage_ = std::make_shared<const TileMapArchive::Storage>(std::move(storage));
    value.validate();
    return value;
}
TileMapData decode_cooked_tile_map(std::span<const std::byte> bytes, AssetId expected) {
    // Full loading fills dense chunks during the same strict admission pass.
    // It neither copies sparse backing nor decodes the cell records twice.
    TileMapData value;
    auto index = TileMapArchive::index(bytes, expected, &value);
    value.asset_ = index.asset;
    value.tile_set_ = index.tile_set;
    value.cell_size_ = index.cell_size;
    value.layers_ = std::move(index.layers);
    value.occupied_ = index.occupied;
    value.admitted_ = true;
    value.validate();
    return value;
}
} // namespace forge
