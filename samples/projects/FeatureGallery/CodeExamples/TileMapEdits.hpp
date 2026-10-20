#pragma once
#include <cstdint>
#include <forge/sdk_client.hpp>

// Call once for an action from a registered fixed-step system. Layer and tile
// keys belong to this actor's Tile Map/Tile Set; tile zero erases a cell.
// A nonzero token is queued, not yet applied. Inspect it with Client::tile_edit
// on a later tick, then release it with Client::release_tile_edit. Stop cancels
// pending changes. Neither this function nor the engine writes source files.
inline std::uint64_t forge_gallery_write_tile(const ForgeSdkWorldV1* host, const char* scene_uuid,
                                              const char* entity_uuid, std::uint64_t layer,
                                              std::int32_t x, std::int32_t y, std::uint64_t tile) {
    forge::sdk::Client client(host);
    ForgeSdkTileMapV1 map{};
    if (!client.callable(forge::sdk::Capability::TileMapEdits) ||
        client.tile_map(scene_uuid, entity_uuid, map) != 1 || !map.ready)
        return 0;
    const ForgeSdkTileWriteV1 edit{{layer, x, y}, {tile, 0, 0}};
    return client.write_tiles(scene_uuid, entity_uuid, map.generation, &edit, 1);
}
