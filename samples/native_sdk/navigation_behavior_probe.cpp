#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/navigation_behavior_components.hpp>
#include <forge/navigation_components.hpp>
#include <map>
#include <memory>
#include <string>
#include <string_view>
namespace {
template <class T> bool layout(flecs::world& world, const char* path) {
    const auto component = world.component<T>(path);
    const auto* info = ecs_get_type_info(world.c_ptr(), component.id());
    return info && info->size == sizeof(T) && info->alignment == alignof(T);
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    if (!layout<forge::NavigationBehavior>(world, "forge.navigation_behavior") ||
        !layout<forge::NavigationBehaviorCursor>(world,
                                                 "forge.runtime.navigation_behavior_cursor") ||
        !layout<forge::NavigationAgent>(world, "forge.navigation_agent") ||
        !layout<forge::NavigationLink>(world, "forge.navigation_link"))
        return 0;
    const forge::NavigationBehavior config{{}, {}, {}, false};
    forge::NavigationBehaviorCursor progress{2, 5, .125, 1};
    progress.tree_status = forge::NavigationBehaviorTreeStatus::Running;
    progress.tree_active = (1u << 31) | 1;
    progress.tree_children[0] = 1;
    progress.tree_children[31] = 7;
    forge::NavigationLink link;
    link.enabled = false;
    link.bidirectional = false;
    link.plane = 1;
    link.profile = std::string(64, 'P');
    link.start_x = -2.25f;
    link.start_y = -.5f;
    link.end_x = 1.75f;
    link.end_y = 3.5f;
    link.radius = .75f;
    const auto expected_link = link;
    auto value = world.entity()
                     .add(flecs::Prefab)
                     .set<forge::NavigationBehavior>(config)
                     .set<forge::NavigationBehaviorCursor>(progress)
                     .set<forge::NavigationLink>(link);
    link.profile[0] = 'Q';
    auto copied = world.entity()
                      .add(flecs::Prefab)
                      .set<forge::NavigationLink>(value.get<forge::NavigationLink>());
    const bool matched = value.get<forge::NavigationBehavior>() == config &&
                         value.get<forge::NavigationBehaviorCursor>() == progress &&
                         value.get<forge::NavigationLink>() == expected_link;
    value.destruct();
    const bool retained = copied.get<forge::NavigationLink>() == expected_link;
    copied.destruct();
    return matched && retained ? 1 : 0;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char*, uint32_t) {
    flecs::world world(host->world);
    auto tokens = std::make_shared<std::map<forge::AssetId, uint64_t>>();
    auto reported = std::make_shared<bool>(false);
    auto link_reported = std::make_shared<bool>(false);
    world.system<const forge::NavigationLink>()
        .kind(host->post_physics_phase)
        .each([host, link_reported](flecs::entity, const forge::NavigationLink& link) {
            if (*link_reported || !link.enabled ||
                (link.profile != "Loop" && link.profile != "OneWay"))
                return;
            *link_reported = true;
            const bool valid =
                link.plane == 1 && (link.profile == "Loop" || link.profile == "OneWay") &&
                link.start_x == -2 && link.end_x == 2 && link.start_z == 0 && link.end_z == 0;
            host->diagnostic(host->context, valid ? 1 : 3,
                             valid ? "Navigation link SDK authored source passed"
                                   : "Navigation link SDK authored source mismatch");
        })
        .add(host->fixed_tag);
    world.system<const forge::NavigationBehavior, const forge::NavigationBehaviorCursor>()
        .kind(host->post_physics_phase)
        .each([host, tokens, reported](flecs::entity, const forge::NavigationBehavior& config,
                                       const forge::NavigationBehaviorCursor& cursor) {
            if (*reported || !config.enabled || !cursor.state)
                return;
            auto [slot, inserted] = tokens->try_emplace(config.graph.id, 0);
            if (inserted) {
                const auto id = config.graph.id.str();
                slot->second = host->resource_request(
                    host->context, FORGE_SDK_RESOURCE_NAVIGATION_BEHAVIOR, id.c_str(), 0);
                if (!slot->second) {
                    host->diagnostic(host->context, 3,
                                     "Navigation behavior SDK resource request failed");
                    *reported = true;
                }
                return; // The existing resource owner adopts the new subscription.
            }
            bool ready = true;
            {
                ForgeSdkResourceV1 status{};
                status.size = sizeof(status);
                if (!host->resource_inspect(host->context, slot->second, &status)) {
                    host->diagnostic(host->context, 3,
                                     "Navigation behavior SDK resource inspection failed");
                    *reported = true;
                    return;
                }
                ready &=
                    std::string_view(status.state) == "ready" && status.retained_revision[0] != 0;
            }
            if (ready) {
                *reported = true;
                if (cursor.tree_active) {
                    if (cursor.tree_status != forge::NavigationBehaviorTreeStatus::Running ||
                        !(cursor.tree_active & 1)) {
                        host->diagnostic(host->context, 3,
                                         "Navigation behavior SDK tree progress invalid");
                        return;
                    }
                    host->diagnostic(host->context, 1,
                                     "Navigation behavior SDK tree progress passed");
                }
                host->diagnostic(host->context, 1,
                                 "Navigation behavior SDK progress/resource passed");
            }
        })
        .add(host->fixed_tag);
    return 1;
}
const char* dependencies[]{"forge.navigation", "forge.resources"};
const ForgeNativeSdkV1 api{sizeof(api),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.navigation_behavior_probe",
                           "1",
                           dependencies,
                           2,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_NAVIGATION | FORGE_SDK_RESOURCES,
                           FORGE_SDK_NAVIGATION | FORGE_SDK_RESOURCES | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
