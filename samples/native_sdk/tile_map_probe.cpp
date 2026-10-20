// Original exact-SDK CPU cell-query/edit integration probe. No private headers.
#include "tile_map_probe.hpp"
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <stdexcept>
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char*, uint32_t) try {
    flecs::world world(host->world);
    world.component<TileMapProbe>("tile_probe.State");
    return 1;
} catch (...) {
    return 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) try {
    flecs::world world(host->world);
    world.entity("tile_probe.state").set<TileMapProbe>({host});
    world.system<TileMapProbe>("tile_probe.Cells")
        .kind(host->fixed_phase)
        .each([](TileMapProbe& probe) {
            if (!probe.stage || probe.stage >= 3)
                return;
            try {
                forge::sdk::Client client(probe.host);
                require(client.callable(forge::sdk::Capability::TileMaps) &&
                            client.callable(forge::sdk::Capability::TileMapEdits),
                        "Fixed tile capabilities unavailable");
                if (probe.stage == 1) {
                    ForgeSdkTileMapV1 info{};
                    require(client.tile_map(probe.scene, probe.entity, info) == 1 && info.ready,
                            "Module cannot inspect ready map");
                    ForgeSdkTileLayerV1 layer{};
                    require(client.tile_layer(probe.scene, probe.entity, info.generation, 0,
                                              layer) == 1,
                            "Module cannot enumerate layer");
                    probe.generation = info.generation;
                    ForgeSdkTileWriteV1 writes[] = {{{layer.key, 0, 0}, {probe.tile, 1, 0}},
                                                    {{layer.key, 0, 0}, {probe.tile, 3, 0}}};
                    probe.token =
                        client.write_tiles(probe.scene, probe.entity, info.generation, writes, 2);
                    require(probe.token != 0, "Module cell batch rejected");
                    writes[1].value = {}; // Callback must already have copied the batch.
                    probe.stage = 2;
                } else {
                    ForgeSdkTileEditV1 edit{};
                    require(client.tile_edit(probe.token, edit) == 1, "Module lost owned token");
                    if (edit.state == FORGE_SDK_TILE_EDIT_PENDING)
                        return;
                    require(edit.state == FORGE_SDK_TILE_EDIT_APPLIED && edit.revision == 1,
                            "Module cell edit failed");
                    ForgeSdkTileLayerV1 layer{};
                    require(client.tile_layer(probe.scene, probe.entity, probe.generation, 0,
                                              layer) == 1,
                            "Module lost layer binding");
                    ForgeSdkTileLocationV1 location{layer.key, 0, 0};
                    ForgeSdkTileCellV1 cell{};
                    require(client.tile_cells(probe.scene, probe.entity, probe.generation,
                                              &location, &cell, 1) == 1 &&
                                cell.tile == probe.tile && cell.flags == 3 && !cell.reserved,
                            "Module query does not see its copied edit");
                    require(client.release_tile_edit(probe.token), "Module cannot release edit");
                    probe.token = 0;
                    probe.stage = 3;
                }
            } catch (const std::exception& error) {
                std::snprintf(probe.error, sizeof(probe.error), "%s", error.what());
                probe.stage = 99;
            }
        })
        .add(host->fixed_tag);
    return 1;
} catch (...) {
    return 0;
}
const char* dependencies[] = {"forge.resources"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.tile_map_probe",
                           "1",
                           dependencies,
                           1,
                           FORGE_SDK_AUTHORING | FORGE_SDK_RUNTIME | FORGE_SDK_VALIDATION,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RESOURCES,
                           FORGE_SDK_RESOURCES,
                           &ecs_init,
                           &ecs_os_api,
                           schema,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
