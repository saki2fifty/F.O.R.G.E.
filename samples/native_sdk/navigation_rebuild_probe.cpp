#include <cstring>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/navigation_components.hpp>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <memory>
#include <string>
#include <thread>

namespace {
struct Probe {
    uint64_t token = 0, control_token = 0;
    bool valid = true, finished = false, controls_finished = false;
    std::string asset;
};
struct ProbeStorage {
    std::shared_ptr<Probe> value;
};
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.component<forge::NavigationAgent>("forge.navigation_agent");
    world.component<forge::NavigationSurface>("forge.navigation_surface");
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.component<ProbeStorage>("project.NavigationRebuildProbeStorage");
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    auto state = std::make_shared<Probe>();
    world.entity("navigation.rebuild.probe").set<ProbeStorage>({state});
    uint64_t outside = 19;
    state->valid = !host->navigation_rebuild_request(
                       host->context, "00000000-0000-4000-8000-000000000001", &outside) &&
                   outside == 0;
    world.system<const forge::NavigationAgent>()
        .kind(host->fixed_phase)
        .each([host, state](const forge::NavigationAgent& agent) {
            if (state->finished)
                return;
            forge::sdk::Client client(host);
            const auto asset = agent.navmesh.id.str();
            state->asset = asset;
            const auto finish = [&](bool valid) {
                client.diagnostic(valid ? 1 : 3, valid ? "Navigation rebuild SDK boundary passed"
                                                       : "Navigation rebuild SDK boundary failed");
                state->finished = true;
            };
#ifdef FORGE_NAVIGATION_REBUILD_DENIED
            uint64_t denied = 17;
            ForgeSdkNavigationRebuildV1 denied_observation{};
            denied_observation.size = sizeof(denied_observation);
            denied_observation.version = 1;
            finish(state->valid && !client.navigation_rebuild_request(asset.c_str(), denied) &&
                   denied == 0 && !client.navigation_rebuild_state(1, denied_observation) &&
                   !client.navigation_rebuild_cancel(1) &&
                   !host->navigation_rebuild_request(host->context, asset.c_str(), &denied) &&
                   !host->navigation_rebuild_state(host->context, 1, &denied_observation) &&
                   !host->navigation_rebuild_cancel(host->context, 1));
            return;
#endif
            if (!state->token) {
                flecs::world current(host->world);
                current.each([](flecs::entity floor, const forge::NavigationSurface& surface,
                                const forge::LocalTranslation& position) {
                    if (surface.enabled && !floor.has(flecs::Prefab))
                        floor.set<forge::LocalTranslation>(
                            {position.x + 40, position.y, position.z});
                });
                bool foreign_rejected = false;
                std::thread foreign([&] {
                    uint64_t token = 42;
                    foreign_rejected =
                        !host->navigation_rebuild_request(host->context, asset.c_str(), &token) &&
                        token == 0;
                });
                foreign.join();
                state->valid &= foreign_rejected &&
                                client.navigation_rebuild_request(asset.c_str(), state->token);
                uint64_t busy = 42;
                state->valid &=
                    !client.navigation_rebuild_request(asset.c_str(), busy) && busy == 0;
                if (!state->valid)
                    finish(false);
                return;
            }
            ForgeSdkNavigationRebuildV1 observation{};
            state->valid &= client.navigation_rebuild_state(state->token, observation);
            if (!state->valid || observation.phase == FORGE_SDK_NAV_REBUILD_FAILED ||
                observation.phase == FORGE_SDK_NAV_REBUILD_STALE) {
                finish(false);
                return;
            }
            if (observation.phase != FORGE_SDK_NAV_REBUILD_APPLIED)
                return;
            const double from[]{34, 0, 0}, to[]{46, 0, 0};
            double points[192]{};
            ForgeSdkNavResultV1 route{sizeof(route), 0, 0, 64, points};
            state->valid &= observation.query_generation > 0 &&
                            std::strlen(observation.geometry_revision) == 64 &&
                            asset == observation.asset_uuid &&
                            client.navigation(asset.c_str(), 1, from, to, route) &&
                            route.status == FORGE_SDK_NAV_SUCCESS && route.count >= 2 &&
                            !client.navigation_rebuild_cancel(state->token);
            observation.version = 2;
            state->valid &=
                !host->navigation_rebuild_state(host->context, state->token, &observation);
            observation.version = 1;
            --observation.size;
            state->valid &=
                !host->navigation_rebuild_state(host->context, state->token, &observation);
            uint64_t next = 0;
            state->valid &=
                client.navigation_rebuild_request(asset.c_str(), next) && next != state->token &&
                !client.navigation_rebuild_state(state->token, observation) &&
                client.navigation_rebuild_cancel(next) && !client.navigation_rebuild_cancel(next) &&
                client.navigation_rebuild_state(next, observation) &&
                observation.phase == FORGE_SDK_NAV_REBUILD_CANCELLED;
            finish(state->valid);
        })
        .add(host->fixed_tag);
    return 1;
}
int32_t FORGE_SDK_CALL controls(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    const auto entity = world.lookup("navigation.rebuild.probe");
    if (!entity || !entity.has<ProbeStorage>())
        return 0;
    const auto state = entity.get<ProbeStorage>().value;
    if (!state->finished || !state->valid || state->controls_finished)
        return 1;
    const forge::sdk::Client client(host);
    const auto finish = [&](bool valid) {
        client.diagnostic(valid ? 1 : 3, valid ? "Navigation rebuild SDK control boundary passed"
                                               : "Navigation rebuild SDK control boundary failed");
        state->controls_finished = true;
    };
#ifdef FORGE_NAVIGATION_REBUILD_DENIED
    uint64_t denied = 17;
    ForgeSdkNavigationRebuildV1 observation{};
    observation.size = sizeof(observation);
    observation.version = 1;
    finish(!client.callable(forge::sdk::Capability::NavigationRebuild) &&
           !client.navigation_rebuild_request(state->asset.c_str(), denied) && denied == 0 &&
           !host->navigation_rebuild_state(host->context, 1, &observation) &&
           !host->navigation_rebuild_cancel(host->context, 1));
#else
    if (!client.callable(forge::sdk::Capability::NavigationRebuild) ||
        client.callable(forge::sdk::Capability::Navigation)) {
        finish(false);
        return 1;
    }
    if (!state->control_token) {
        if (!client.navigation_rebuild_request(state->asset.c_str(), state->control_token))
            finish(false);
        return 1;
    }
    ForgeSdkNavigationRebuildV1 observation{};
    if (!client.navigation_rebuild_state(state->control_token, observation))
        finish(false);
    else if (observation.phase >= FORGE_SDK_NAV_REBUILD_APPLIED)
        finish(observation.phase == FORGE_SDK_NAV_REBUILD_APPLIED);
#endif
    return 1;
}
const char* dependencies[]{"forge.navigation", "forge.input"};
#ifdef FORGE_NAVIGATION_REBUILD_DENIED
constexpr uint32_t navigation_capability = 0;
#else
constexpr uint32_t navigation_capability = FORGE_SDK_NAVIGATION;
#endif
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.navigation_rebuild_probe",
                           "1",
                           dependencies,
                           2,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           navigation_capability,
                           navigation_capability | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr,
                           controls};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
