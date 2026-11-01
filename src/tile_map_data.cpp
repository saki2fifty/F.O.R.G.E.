#include "tile_map_data.hpp"
#include "tile_map_chunk_codec.hpp"
#include "tile_set_data.hpp"
#include <algorithm>
#include <set>

namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
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
std::size_t TileMapData::chunk_occupied(const ChunkStorage& value) {
    return std::visit([](const auto& chunk) { return chunk->occupied; }, value);
}
TileMapCell TileMapData::chunk_cell(const ChunkStorage& value, std::size_t slot) {
    if (const auto* dense = std::get_if<std::shared_ptr<const Chunk>>(&value))
        return (*dense)->cells[slot];
    const auto& encoded = *std::get<std::shared_ptr<const EncodedChunk>>(value);
    return tile_detail::admitted_chunk_cell(encoded.records(), std::uint32_t(slot));
}
std::array<TileMapCell, tile_chunk_cells> TileMapData::chunk_cells(const ChunkStorage& value) {
    if (const auto* dense = std::get_if<std::shared_ptr<const Chunk>>(&value))
        return (*dense)->cells;
    const auto& encoded = *std::get<std::shared_ptr<const EncodedChunk>>(value);
    return tile_detail::decode_admitted_cells(encoded.records(), encoded.occupied);
}
TileMapCell TileMapData::cell(std::uint64_t layer, TileCellAddress cell) const {
    validate();
    require(has_layer(layer), "Cooked Tile Map query has no matching layer");
    const auto address = tile_chunk_cell(cell);
    const auto found = chunks_.find({layer, address.chunk});
    return found == chunks_.end() ? TileMapCell{} : chunk_cell(found->second, address.index);
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
        const auto count = old == chunks_.end() ? 0 : chunk_occupied(old->second);
        patch.occupied = count;
        for (const auto& [index, cell] : patch.cells) {
            if (old != chunks_.end() && chunk_cell(old->second, index).tile)
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
TileMapData TileMapData::with_compact_cells(std::span<const TileMapCellEdit> writes,
                                            const TileSetData& definitions) const {
    return apply_cells(prepare_cells(writes, &definitions), true);
}
TileMapData TileMapData::with_erased_cells(std::span<const TileMapCellEdit> writes) const {
    return apply_cells(prepare_cells(writes, nullptr));
}
TileMapData TileMapData::apply_cells(const CellChanges& changes, bool compact) const {
    auto next = *this;
    for (const auto& [key, patch] : changes.chunks) {
        const auto old = chunks_.find(key);
        if (!patch.occupied) {
            next.chunks_.erase(key);
            continue;
        }
        if (old != chunks_.end() &&
            std::all_of(patch.cells.begin(), patch.cells.end(), [&](const auto& cell) {
                return chunk_cell(old->second, cell.first) == cell.second;
            }))
            continue;
        auto chunk = std::make_shared<Chunk>();
        if (old != chunks_.end())
            chunk->cells = chunk_cells(old->second);
        for (const auto& [index, cell] : patch.cells)
            chunk->cells[index] = cell;
        chunk->occupied = patch.occupied;
        if (compact) {
            std::vector<std::byte> records;
            records.reserve(patch.occupied * 16);
            tile_detail::append_chunk_cells(records, chunk->cells);
            auto encoded = std::make_shared<EncodedChunk>();
            encoded->bytes = std::make_shared<const std::vector<std::byte>>(std::move(records));
            encoded->occupied = patch.occupied;
            next.chunks_[key] = std::move(encoded);
        } else {
            next.chunks_[key] = std::move(chunk);
        }
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
    // Preserve borrowed-span lifetime for the existing dense API. A compact
    // value requires the explicit scoped visitor, rather than temporary pointers.
    require(std::all_of(chunks_.begin(), chunks_.end(),
                        [](const auto& row) {
                            return std::holds_alternative<std::shared_ptr<const Chunk>>(row.second);
                        }),
            "Compact Tile Map requires a scoped chunk-value visitor");
    for (const auto& [key, storage] : chunks_) {
        const auto& chunk = *std::get<std::shared_ptr<const Chunk>>(storage);
        visit(key, chunk.cells, chunk.occupied);
    }
}
void TileMapData::for_each_chunk_values(
    const std::function<void(TileMapChunkKey, std::span<const TileMapCell, tile_chunk_cells>,
                             std::size_t)>& visit) const {
    validate();
    require(bool(visit), "Cooked Tile Map scoped chunk visitor is empty");
    for (const auto& [key, storage] : chunks_)
        visit_chunk_storage(storage,
                            [&](auto cells, auto occupied) { visit(key, cells, occupied); });
}
void TileMapData::visit_chunk_storage(
    const ChunkStorage& storage,
    const std::function<void(std::span<const TileMapCell, tile_chunk_cells>, std::size_t)>& visit) {
    if (const auto* dense = std::get_if<std::shared_ptr<const Chunk>>(&storage))
        visit((*dense)->cells, (*dense)->occupied);
    else {
        const auto cells = chunk_cells(storage);
        visit(cells, chunk_occupied(storage));
    }
}
bool TileMapData::visit_chunk_values(
    TileMapChunkKey key,
    const std::function<void(std::span<const TileMapCell, tile_chunk_cells>, std::size_t)>& visit)
    const {
    validate();
    require(bool(visit), "Cooked Tile Map scoped chunk visitor is empty");
    require(has_layer(key.layer), "Cooked Tile Map chunk query has no matching layer");
    (void)tile_cell_address({key.address, 0});
    (void)tile_cell_address({key.address, tile_chunk_cells - 1});
    const auto found = chunks_.find(key);
    if (found == chunks_.end())
        return false;
    visit_chunk_storage(found->second, visit);
    return true;
}
void TileMapData::for_each_chunk_info(
    const std::function<void(TileMapChunkKey, std::size_t, const void*)>& visit) const {
    validate();
    require(bool(visit), "Cooked Tile Map chunk-info visitor is empty");
    for (const auto& [key, storage] : chunks_)
        std::visit([&](const auto& chunk) { visit(key, chunk->occupied, chunk.get()); }, storage);
}
std::size_t TileMapData::resident_bytes() const {
    const std::array<const TileMapData*, 1> values{this};
    return resident_bytes(values);
}
std::size_t TileMapData::dense_reconstruction_bytes() const {
    validate();
    std::size_t bytes =
        sizeof(*this) + layers_.capacity() * sizeof(TileMapLayer) +
        chunks_.size() * (sizeof(TileMapChunkKey) + sizeof(ChunkStorage) + sizeof(Chunk) + 64);
    for (const auto& layer : layers_)
        bytes += layer.name.capacity() + 1;
    return bytes;
}
std::size_t TileMapData::resident_bytes(std::span<const TileMapData* const> views) {
    std::set<const TileMapData*> values;
    std::set<const Chunk*> dense;
    std::set<const EncodedChunk*> encoded;
    std::set<const std::vector<std::byte>*> backing;
    std::size_t bytes = 0;
    for (const auto* value : views) {
        require(value, "Tile Map memory estimate received a null view");
        if (!values.insert(value).second)
            continue;
        value->validate();
        bytes += sizeof(*value) + value->layers_.capacity() * sizeof(TileMapLayer) +
                 value->chunks_.size() * (sizeof(TileMapChunkKey) + sizeof(ChunkStorage) + 64);
        for (const auto& layer : value->layers_)
            bytes += layer.name.capacity() + 1;
        for (const auto& [key, storage] : value->chunks_) {
            (void)key;
            if (const auto* chunk = std::get_if<std::shared_ptr<const Chunk>>(&storage)) {
                if (dense.insert(chunk->get()).second)
                    bytes += sizeof(Chunk);
            } else {
                const auto* archived = std::get<std::shared_ptr<const EncodedChunk>>(storage).get();
                if (encoded.insert(archived).second) {
                    bytes += sizeof(EncodedChunk);
                    if (backing.insert(archived->bytes.get()).second)
                        bytes += sizeof(std::vector<std::byte>) + 64 + archived->bytes->capacity();
                }
            }
        }
    }
    return bytes;
}
TileMapChunkInput TileMapData::chunk_input(TileMapChunkKey key) const {
    validate();
    require(has_layer(key.layer), "Cooked Tile Map chunk input has no matching layer");
    (void)tile_cell_address({key.address, 0});
    (void)tile_cell_address({key.address, tile_chunk_cells - 1});
    const auto found = chunks_.find(key);
    require(found != chunks_.end(), "Tile Map has no requested chunk input");
    TileMapChunkInput input;
    input.asset_ = asset_;
    input.key_ = key;
    input.storage_ = found->second;
    return input;
}
void TileMapChunkInput::validate() const {
    require(bool(asset_) && std::visit([](const auto& value) { return bool(value); }, storage_),
            "Tile Map chunk input is unavailable or was moved from");
}
AssetId TileMapChunkInput::asset() const {
    validate();
    return asset_;
}
TileMapChunkKey TileMapChunkInput::key() const {
    validate();
    return key_;
}
std::size_t TileMapChunkInput::occupied() const {
    validate();
    return TileMapData::chunk_occupied(storage_);
}
const void* TileMapChunkInput::identity() const {
    validate();
    return std::visit([](const auto& value) -> const void* { return value.get(); }, storage_);
}
const void* TileMapChunkInput::backing_identity() const {
    validate();
    if (const auto* dense = std::get_if<std::shared_ptr<const TileMapData::Chunk>>(&storage_))
        return dense->get();
    return std::get<std::shared_ptr<const TileMapData::EncodedChunk>>(storage_)->bytes.get();
}
std::size_t TileMapChunkInput::backing_bytes() const {
    validate();
    if (std::holds_alternative<std::shared_ptr<const TileMapData::Chunk>>(storage_))
        return sizeof(TileMapData::Chunk) + 64;
    const auto& bytes = std::get<std::shared_ptr<const TileMapData::EncodedChunk>>(storage_)->bytes;
    return sizeof(std::vector<std::byte>) + 64 + bytes->capacity();
}
std::size_t TileMapChunkInput::input_bytes() const {
    validate();
    return sizeof(*this) + 64 +
           (std::holds_alternative<std::shared_ptr<const TileMapData::EncodedChunk>>(storage_)
                ? sizeof(TileMapData::EncodedChunk) + 64
                : 0);
}
std::optional<std::span<const std::byte>> TileMapChunkInput::encoded_cell_records() const {
    validate();
    if (const auto* encoded =
            std::get_if<std::shared_ptr<const TileMapData::EncodedChunk>>(&storage_))
        return (*encoded)->records();
    return {};
}
TileMapChunkData TileMapChunkInput::decode(std::stop_token stop) const {
    validate();
    require(!stop.stop_requested(), "Tile Map chunk decoding cancelled");
    if (const auto* encoded =
            std::get_if<std::shared_ptr<const TileMapData::EncodedChunk>>(&storage_))
        return {
            key_,
            tile_detail::decode_admitted_cells((*encoded)->records(), (*encoded)->occupied, stop),
            (*encoded)->occupied};
    const auto& dense = *std::get<std::shared_ptr<const TileMapData::Chunk>>(storage_);
    TileMapChunkData value{key_, dense.cells, dense.occupied};
    require(!stop.stop_requested(), "Tile Map chunk decoding cancelled");
    return value;
}
} // namespace forge
