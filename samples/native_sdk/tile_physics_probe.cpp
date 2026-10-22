// Original FORGE public-SDK integration fixture. No private or vendor physics APIs.
#include "PaintedCollision.hpp"
#include <cmath>
#include <cstdio>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/physics_components.hpp>
#include <map>
#include <memory>
#include <stdexcept>

namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
int32_t FORGE_SDK_CALL schema(const ForgeSdkWorldV1* host, char*, uint32_t) try {
    flecs::world(host->world).component<forge::TileMapCollider2D>("forge.tile_map_collider_2d");
    return 1;
} catch (...) {
    return 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) try {
    auto requests = std::make_shared<std::map<std::uint64_t, std::uint64_t>>();
    flecs::world world(host->world);
    world.system<const forge::TileMapCollider2D>()
        .kind(host->fixed_phase)
        .each([host, requests](flecs::entity entity, const forge::TileMapCollider2D& collider) {
            forge::sdk::Client client(host);
            ForgeSdkEntityRefV1 actor{};
            require(client.entity_reference(entity.id(), actor) == 1,
                    "Painted SDK cannot obtain the canonical Map actor reference");
            if (const auto found = requests->find(entity.id()); found != requests->end()) {
                ForgeSdkTileEditV1 edit{};
                require(client.tile_edit(found->second, edit) == 1,
                        "Painted SDK lost its owned request");
                if (edit.state == FORGE_SDK_TILE_EDIT_PENDING)
                    return;
                require(edit.state == FORGE_SDK_TILE_EDIT_APPLIED,
                        "Painted SDK collision edit failed admission");
                require(client.release_tile_edit(found->second), "Painted SDK cannot release edit");
                requests->erase(found);
                require(host->diagnostic(host->context, 1, "Painted SDK edit applied") == 1,
                        "Painted SDK diagnostic unavailable");
            }
            ForgeSdkTileMapV1 map{};
            require(client.tile_map(actor.scene_uuid, actor.entity_uuid, map) == 1 && map.ready,
                    "Painted SDK Map is not ready before the fixed system");
            const ForgeSdkTileLocationV1 location{collider.layer, -4, 0};
            ForgeSdkTileCellV1 cell{};
            require(client.tile_cells(actor.scene_uuid, actor.entity_uuid, map.generation,
                                      &location, &cell, 1) == 1,
                    "Painted SDK cannot read selected solid cells");
            if (cell.tile) {
                const auto token =
                    forge_gallery_remove_painted_platform(host, actor, collider.layer);
                require(token != 0, "Painted SDK could not queue its copied cell batch");
                requests->emplace(entity.id(), token);
            }
        })
        .add(host->fixed_tag);
    world.system<>()
        .kind(host->post_physics_phase)
        .run([host](flecs::iter&) {
            ForgeSdkPhysicsHit2DV1 hit{};
            require(forge_gallery_query_painted_floor(host, hit) &&
                        std::abs(hit.fraction - .625) < .001,
                    "Painted SDK ray did not query selected compound geometry");
        })
        .add(host->fixed_tag);
    return 1;
} catch (const std::exception& failure) {
    if (capacity)
        std::snprintf(error, capacity, "%s", failure.what());
    return 0;
}
const char* dependencies[] = {"forge.physics", "forge.resources"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.tile_physics_probe",
                           "1",
                           dependencies,
                           2,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_PHYSICS | FORGE_SDK_RESOURCES,
                           FORGE_SDK_PHYSICS | FORGE_SDK_RESOURCES | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schema,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
