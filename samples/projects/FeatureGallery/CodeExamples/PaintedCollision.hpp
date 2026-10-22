#pragma once
#include <forge/sdk_client.hpp>

// Gallery77: call once from a fixed-step C++ system. This queues removal of
// the three left-platform cells in the selected solid layer. The next admitted
// fixed step publishes both cells and collision together; a nonzero token alone
// does not mean success. Inspect Client::tile_edit, then release_tile_edit.
inline std::uint64_t forge_gallery_remove_painted_platform(const ForgeSdkWorldV1* host,
                                                           const ForgeSdkEntityRefV1& actor,
                                                           std::uint64_t solid_layer) {
    forge::sdk::Client client(host);
    ForgeSdkTileMapV1 map{};
    if (!client.callable(forge::sdk::Capability::TileMapEdits) ||
        client.tile_map(actor.scene_uuid, actor.entity_uuid, map) != 1 || !map.ready)
        return 0;
    const ForgeSdkTileWriteV1 erase[] = {
        {{solid_layer, -5, 0}, {}}, {{solid_layer, -4, 0}, {}}, {{solid_layer, -3, 0}, {}}};
    return client.write_tiles(actor.scene_uuid, actor.entity_uuid, map.generation, erase, 3);
}

// Read after Physics. Decorative cells are excluded automatically by the Map's
// selected collision layer; the normal query still returns one actor reference.
inline bool forge_gallery_query_painted_floor(const ForgeSdkWorldV1* host,
                                              ForgeSdkPhysicsHit2DV1& hit) {
    const double origin[] = {-5.5, 2}, displacement[] = {0, -8};
    return forge::sdk::Client(host).raycast_2d(origin, displacement, UINT32_MAX, false, hit) == 1;
}
