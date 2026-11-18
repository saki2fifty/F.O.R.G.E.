#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/navigation_components.hpp>
#include <forge/sdk_client.hpp>
#include <forge/transform_components.hpp>
#include <limits>
#include <memory>
#include <thread>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world.component<forge::NavigationAgent>("forge.navigation_agent");
    world.component<forge::LocalTranslation>("forge.local_translation");
    world.component<forge::NavigationAreaFilter>("forge.navigation_area_filter")
        .member<std::uint32_t>("mask", 0, offsetof(forge::NavigationAreaFilter, mask))
        .member<float>("costs", 16, offsetof(forge::NavigationAreaFilter, costs));
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    world
        .system<const forge::NavigationAgent, const forge::LocalTranslation,
                const forge::NavigationAreaFilter>()
        .kind(host->fixed_phase)
        .each([host, finished = std::make_shared<bool>(false)](
                  const forge::NavigationAgent& agent, const forge::LocalTranslation& position,
                  const forge::NavigationAreaFilter& authored) {
            if (*finished)
                return;
            const auto asset = agent.navmesh.id.str();
            const double from[]{position.x, position.y, position.z};
            const double to[]{agent.destination_x, agent.destination_y, agent.destination_z};
            double points[192]{};
            ForgeSdkNavResultV1 out{sizeof(out), 0, 0, 64, points};
            forge::sdk::Client client(host);
#ifdef FORGE_NAVIGATION_AREAS_DENIED
            ForgeSdkNavigationFilterV1 denied_request{sizeof(denied_request), 1, 65535, {}};
            for (auto& cost : denied_request.costs)
                cost = 1;
            const bool denied =
                !client.navigation(asset.c_str(), 1, from, to, authored, out) &&
                out.status == FORGE_SDK_NAV_UNAVAILABLE && out.count == 0 &&
                host->navigation_query_filtered(host->context, asset.c_str(), 1, from, to,
                                                &denied_request, &out) == 1 &&
                out.status == FORGE_SDK_NAV_UNAVAILABLE && out.count == 0;
            host->diagnostic(host->context, denied ? 1 : 3,
                             denied ? "Navigation area SDK boundary passed"
                                    : "Navigation area SDK boundary failed");
            *finished = true;
            return;
#endif
            if (!client.navigation(asset.c_str(), 1, from, to, authored, out) ||
                out.status == FORGE_SDK_NAV_UNAVAILABLE || out.status == FORGE_SDK_NAV_MISSING)
                return;
            bool valid = out.status == FORGE_SDK_NAV_SUCCESS && out.count > 2 &&
                         authored.mask == 65535 && authored.costs[2] == 20;
            forge::NavigationAreaFilter empty;
            empty.mask = 0;
            valid &= client.navigation(asset.c_str(), 1, from, to, empty, out) &&
                     out.status == FORGE_SDK_NAV_START_OUTSIDE && out.count == 0;
            forge::NavigationAreaFilter ordinary;
            valid &= client.navigation(asset.c_str(), 1, from, to, ordinary, out) &&
                     out.status == FORGE_SDK_NAV_SUCCESS && out.count == 2;
            valid &= client.navigation(asset.c_str(), 1, from, to, out) &&
                     out.status == FORGE_SDK_NAV_SUCCESS && out.count == 2;
            ForgeSdkNavigationFilterV1 request{sizeof(request), 1, 65535, {}};
            for (auto& cost : request.costs)
                cost = 1;
            const auto raw = [&] {
                return host->navigation_query_filtered(host->context, asset.c_str(), 1, from, to,
                                                       &request, &out);
            };
            out.capacity = 1;
            valid &= raw() == 1 && out.status == FORGE_SDK_NAV_LIMIT && out.count == 0;
            out.capacity = 64;
            request.version = 2;
            valid &= raw() == 0 && out.status == FORGE_SDK_NAV_INVALID && out.count == 0;
            request.version = 1;
            request.size = sizeof(request) - 1;
            valid &= raw() == 0 && out.status == FORGE_SDK_NAV_INVALID && out.count == 0;
            request.size = sizeof(request);
            request.mask = 65536;
            valid &= raw() == 1 && out.status == FORGE_SDK_NAV_INVALID && out.count == 0;
            request.mask = 65535;
            for (auto invalid : {0.f, std::numeric_limits<float>::quiet_NaN()}) {
                request.costs[2] = invalid;
                valid &= raw() == 1 && out.status == FORGE_SDK_NAV_INVALID && out.count == 0;
            }
            request.costs[2] = 1;
            bool foreign_rejected = false;
            std::thread foreign([&] {
                foreign_rejected =
                    raw() == 1 && out.status == FORGE_SDK_NAV_INVALID && out.count == 0;
            });
            foreign.join();
            valid &= foreign_rejected;
            const double center[]{0, position.y, position.z};
            request.mask = 1;
            valid &= host->navigation_query_filtered(host->context, asset.c_str(), 0, center,
                                                     nullptr, &request, &out) == 1 &&
                     out.status == FORGE_SDK_NAV_START_OUTSIDE && out.count == 0;
            request.mask = 4;
            valid &= host->navigation_query_filtered(host->context, asset.c_str(), 0, center,
                                                     nullptr, &request, &out) == 1 &&
                     out.status == FORGE_SDK_NAV_SUCCESS && out.count == 1;
            valid &= host->navigation_query_filtered(host->context, asset.c_str(), 0, center,
                                                     nullptr, nullptr, &out) == 0 &&
                     out.status == FORGE_SDK_NAV_INVALID && out.count == 0;
            request.mask = 65535;
            valid &= raw() == 1 && out.status == FORGE_SDK_NAV_SUCCESS && out.count == 2;
            host->diagnostic(host->context, valid ? 1 : 3,
                             valid ? "Navigation area SDK boundary passed"
                                   : "Navigation area SDK boundary failed");
            *finished = true;
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[]{"forge.navigation", "forge.input"};
#ifdef FORGE_NAVIGATION_AREAS_DENIED
constexpr uint32_t navigation_capability = 0;
#else
constexpr uint32_t navigation_capability = FORGE_SDK_NAVIGATION;
#endif
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.navigation_area_probe",
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
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
