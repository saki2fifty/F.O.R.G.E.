// Original native subshape-work acceptance through the public exact SDK.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <flecs.h>
#include <forge/native_sdk_identity.h>
#include <forge/sdk_client.hpp>
#include <stdexcept>

namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void prove(const ForgeSdkWorldV1* host) {
    forge::sdk::Client physics(host);
    const std::array<double, 3> origin{-1, 0, 0}, long_ray{26, 0, 0};
    // 16385 original triangles at x=i*.001. This endpoint intersects exactly
    // 16384; the longer segment intersects all of them and the farther box.
    const std::array<double, 3> exact_ray{1 + 16383.5 * .001, 0, 0};
    std::array<ForgeSdkPhysicsHitV1, 4> hits{};
    require(physics.raycast_all(origin, exact_ray, UINT32_MAX, true, hits.data(), hits.size()) ==
                    1 &&
                std::abs(hits[0].fraction - 1 / exact_ray[0]) < 1e-6,
            "Exact native ray work bound rejected one complete mesh object");
    ForgeSdkEntityRefV1 heavy{};
    heavy.size = sizeof(heavy);
    heavy.version = 1;
    std::memcpy(heavy.scene_uuid, hits[0].scene, 37);
    std::memcpy(heavy.entity_uuid, hits[0].entity, 37);
    const ForgeSdkPhysicsExclusionsV1 excluded{sizeof(excluded), 1, &heavy};
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        for (auto& hit : hits) {
            hit = {};
            hit.size = sizeof(hit);
            hit.scene[0] = hit.entity[0] = 'x';
            hit.fraction = hit.position[0] = hit.normal[0] = 42;
        }
        require(host->physics_raycast_all(host->context, origin.data(), long_ray.data(), UINT32_MAX,
                                          1, hits.data(), hits.size()) == -1 &&
                    std::all_of(hits.begin(), hits.end(),
                                [](const auto& hit) {
                                    return hit.size == sizeof(hit) && !hit.scene[0] &&
                                           !hit.entity[0] && hit.fraction == 0 &&
                                           std::all_of(std::begin(hit.position),
                                                       std::end(hit.position),
                                                       [](double x) { return x == 0; }) &&
                                           std::all_of(std::begin(hit.normal), std::end(hit.normal),
                                                       [](double x) { return x == 0; });
                                }),
                "Native subshape overflow returned a partial or stale SDK result");
        require(physics.raycast_all(origin, long_ray, UINT32_MAX, true, hits.data(), hits.size()) ==
                    -1,
                "C++ client admitted an excessive native subshape traversal");
        require(host->physics_raycast_all_excluding(host->context, origin.data(), long_ray.data(),
                                                    UINT32_MAX, 1, &excluded, hits.data(),
                                                    hits.size()) == 1 &&
                    std::abs(hits[0].fraction - 22.5 / 26) < 1e-6 &&
                    std::strcmp(hits[0].entity, heavy.entity_uuid) != 0,
                "Canonical exclusion did not skip excessive native work and reveal the box");
        require(physics.raycast_all_excluding(origin, long_ray, UINT32_MAX, true,
                                              std::span(&heavy, 1), hits.data(),
                                              hits.size()) == 1 &&
                    std::abs(hits[0].fraction - 22.5 / 26) < 1e-6,
                "C++ canonical exclusion did not skip native subshape work");
        require(physics.raycast_all(origin, long_ray, 2, true, hits.data(), hits.size()) == 1 &&
                    std::abs(hits[0].fraction - 22.5 / 26) < 1e-6,
                "Layer filtering did not skip native subshape work");
        ForgeSdkPhysicsHitV1 closest{};
        closest.size = sizeof(closest);
        require(
            physics.raycast(origin.data(), long_ray.data(), closest) == 1 &&
                std::abs(closest.fraction - 1. / 26) < 1e-6 &&
                physics.raycast_all(origin, exact_ray, UINT32_MAX, true, hits.data(),
                                    hits.size()) == 1,
            "A rejected complete ray changed optimized closest or subsequent exact-bound reads");
    }
    require(host->diagnostic(host->context, 1, "Native ray subshape work proof passed") == 1,
            "Native ray work proof diagnostic rejected");
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1*, char*, uint32_t) { return 1; }
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world(host->world)
        .system<>()
        .kind(host->post_physics_phase)
        .run([host](flecs::iter&) { prove(host); })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[] = {"forge.physics"};
const ForgeNativeSdkV1 api = {sizeof(ForgeNativeSdkV1),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.physics_ray_work",
                              "1",
                              dependencies,
                              1,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
                              0,
                              FORGE_SDK_DIAGNOSTICS | FORGE_SDK_PHYSICS,
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr,
                              nullptr,
                              nullptr,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
