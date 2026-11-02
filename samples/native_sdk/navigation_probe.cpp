#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/navigation_components.hpp>
#include <forge/sdk_client.hpp>
#include <memory>
#include <thread>
namespace {
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    w.component<forge::NavigationAgent>("forge.navigation_agent");
    const auto obstacle = w.component<forge::NavigationObstacle>("forge.navigation_obstacle");
    const auto* obstacle_info = ecs_get_type_info(w.c_ptr(), obstacle.id());
    if (!obstacle_info || obstacle_info->size != sizeof(forge::NavigationObstacle) ||
        obstacle_info->alignment != alignof(forge::NavigationObstacle))
        return 0;
    auto obstacle_probe =
        w.entity().add(flecs::Prefab).set<forge::NavigationObstacle>({false, .75f, 1.5f});
    const bool obstacle_matched = obstacle_probe.get<forge::NavigationObstacle>() ==
                                  forge::NavigationObstacle{false, .75f, 1.5f};
    obstacle_probe.destruct();
    if (!obstacle_matched)
        return 0;
    const auto avoidance = w.component<forge::NavigationAvoidance>("forge.navigation_avoidance");
    const auto* info = ecs_get_type_info(w.c_ptr(), avoidance.id());
    if (!info || info->size != sizeof(forge::NavigationAvoidance))
        return 0;
    auto probe = w.entity().add(flecs::Prefab).set<forge::NavigationAvoidance>({false, .75f});
    const auto matched =
        probe.get<forge::NavigationAvoidance>() == forge::NavigationAvoidance{false, .75f};
    probe.destruct();
    if (!matched)
        return 0;
#ifdef FORGE_NAVIGATION_PLANAR
    const auto region = w.component<forge::NavigationRegion2D>("forge.navigation_region_2d");
    const auto* region_info = ecs_get_type_info(w.c_ptr(), region.id());
    if (!region_info || region_info->size != sizeof(forge::NavigationRegion2D) ||
        region_info->alignment != alignof(forge::NavigationRegion2D))
        return 0;
    forge::NavigationRegion2D expected{true, true, 2, 4};
    expected.points = {{-1, -2}, {1, -2}, {1, 0}, {0, 0}, {0, 2}, {-1, 2}};
    auto planar = w.entity().add(flecs::Prefab).set<forge::NavigationRegion2D>(expected);
    auto copied = w.entity()
                      .add(flecs::Prefab)
                      .set<forge::NavigationRegion2D>(planar.get<forge::NavigationRegion2D>());
    expected.points[1].x = 2;
    planar.set<forge::NavigationRegion2D>(expected);
    bool region_matched = planar.get<forge::NavigationRegion2D>() == expected &&
                          copied.get<forge::NavigationRegion2D>().points[1].x == 1;
    planar.remove<forge::NavigationRegion2D>();
    region_matched = region_matched && copied.get<forge::NavigationRegion2D>().points.size() == 6;
    copied.destruct();
    planar.destruct();
    if (!region_matched)
        return 0;
#endif
    return 1;
}
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* h, char*, uint32_t) {
    flecs::world w(h->world);
    auto first = std::make_shared<bool>(true);
    ForgeSdkNavigationAgentV1 startup{sizeof(startup)};
    if (h->navigation_agent_state(h->context, "81e0fb8b-3d69-4aa9-8b17-41a22b034e61",
                                  "8f193aa1-64b9-4cfb-8825-080700f1b0ed", &startup) != 0 ||
        startup.version != 1 || startup.sampled)
        return 0;
#ifdef FORGE_NAVIGATION_DENIED
    w.system<const forge::NavigationAgent>()
        .kind(h->fixed_phase)
        .each([h](flecs::entity entity, const forge::NavigationAgent&) {
            forge::sdk::Client client(h);
            ForgeSdkEntityRefV1 reference{};
            ForgeSdkNavigationAgentV1 out{};
            out.size = sizeof(out);
            out.tick = 123;
            auto ok = client.entity_reference(entity.id(), reference) == 1 &&
                      !client.available(forge::sdk::Capability::Navigation) &&
                      h->navigation_agent_state(h->context, reference.scene_uuid,
                                                reference.entity_uuid, &out) == 0 &&
                      out.tick == 0 && !out.sampled && out.status == FORGE_SDK_NAV_UNAVAILABLE;
            out.tick = 123;
            ok = ok &&
                 client.navigation_agent(reference.scene_uuid, reference.entity_uuid, out) == 0 &&
                 out.tick == 0 && !out.sampled && out.status == FORGE_SDK_NAV_UNAVAILABLE;
            h->diagnostic(h->context, ok ? 1 : 3,
                          ok ? "navigation agent permission denial passed"
                             : "navigation agent permission denial failed");
        })
        .add(h->fixed_tag);
    return 1;
#endif
    w.system<const forge::NavigationAgent>()
        .kind(h->fixed_phase)
        .each([h, first](flecs::entity entity, const forge::NavigationAgent& a) {
            forge::sdk::Client client(h);
            ForgeSdkEntityRefV1 reference{};
            ForgeSdkNavigationAgentV1 state{};
            bool valid =
                client.entity_reference(entity.id(), reference) == 1 &&
                client.navigation_agent(reference.scene_uuid, reference.entity_uuid, state) == 1;
            if (*first) {
                *first = false;
#ifdef FORGE_NAVIGATION_PLANAR
                unsigned regions = 0;
                bool polygon_values = true;
                flecs::world current(h->world);
                current.each([&](const forge::NavigationRegion2D& region) {
                    ++regions;
                    polygon_values =
                        polygon_values && region.points.size() == (region.obstacle ? 6u : 4u);
                });
                if (regions != 2 || !polygon_values)
                    valid = false;
                h->diagnostic(h->context, polygon_values && regions == 2 ? 1 : 3,
                              valid ? "navigation polygon native values passed"
                                    : "navigation polygon native values failed");
#endif

                valid = valid && state.phase == FORGE_SDK_NAV_AGENT_PENDING && !state.sampled;
                ForgeSdkNavigationAgentV1 bad{};
                bad.size = 1;
                bad.tick = 123;
                valid = valid &&
                        h->navigation_agent_state(h->context, reference.scene_uuid,
                                                  reference.entity_uuid, &bad) == -1 &&
                        bad.tick == 123;
                bad.size = sizeof(bad);
                valid = valid &&
                        h->navigation_agent_state(h->context, "bad", reference.entity_uuid, &bad) ==
                            -1 &&
                        bad.tick == 0;
                int32_t foreign = 0;
                std::thread worker([&] {
                    foreign = h->navigation_agent_state(h->context, reference.scene_uuid,
                                                        reference.entity_uuid, &bad);
                });
                worker.join();
                valid = valid && foreign == -1 && !bad.sampled && bad.tick == 0;
                h->diagnostic(h->context, valid ? 1 : 3,
                              valid ? "navigation agent boundary passed"
                                    : "navigation agent boundary failed");
            }
            if (valid && state.phase == FORGE_SDK_NAV_AGENT_ARRIVED) {
                auto next = a;
                next.destination_x = -a.destination_x;
                entity.set<forge::NavigationAgent>(next);
                h->diagnostic(h->context, 1, "navigation agent arrived; C++ changed destination");
            }
            const auto id = a.navmesh.id.str();
            double start[3]{-8, .1, 0}, end[3]{8, .1, 0}, points[192]{};
#ifdef FORGE_NAVIGATION_PLANAR
            start[2] = 37.125;
            end[2] = -19;
#endif
            ForgeSdkNavResultV1 out{sizeof(out), 0, 0, 64, points};
            bool ok = (h->capabilities & FORGE_SDK_NAVIGATION) &&
                      h->navigation_query(h->context, id.c_str(), 1, start, end, &out) &&
                      out.status == 0 && out.count > 2;
#ifdef FORGE_NAVIGATION_PLANAR
            for (unsigned i = 0; i < out.count && i < 64; ++i)
                ok = ok && points[i * 3 + 2] == 37.125;
#endif
            out.capacity = 1;
            ok = ok && h->navigation_query(h->context, id.c_str(), 1, start, end, &out) &&
                 out.status == 7 && out.count == 0;
            ok = ok && h->navigation_query(h->context, id.c_str(), 0, start, nullptr, &out) &&
                 out.status == 0 && out.count == 1;
            h->diagnostic(h->context, ok ? 1 : 3,
                          ok ? "navigation SDK queries passed" : "navigation SDK query failed");
        })
        .add(h->fixed_tag);
    w.system<const forge::NavigationAgent>()
        .kind(h->post_physics_phase)
        .each([h](flecs::entity entity, const forge::NavigationAgent&) {
            forge::sdk::Client client(h);
            ForgeSdkEntityRefV1 reference{};
            ForgeSdkNavigationAgentV1 state{};
            const auto ok =
                client.entity_reference(entity.id(), reference) == 1 &&
                client.navigation_agent(reference.scene_uuid, reference.entity_uuid, state) == 1 &&
                state.sampled && state.has_path && state.status == FORGE_SDK_NAV_SUCCESS &&
                state.tick > 0;
            h->diagnostic(h->context, ok ? 1 : 3,
                          ok ? "navigation agent PostPhysics sample passed"
                             : "navigation agent PostPhysics sample failed");
        })
        .add(h->fixed_tag);
    return 1;
}
const char* deps[]{"forge.navigation", "forge.input"};
const ForgeNativeSdkV1 api = {sizeof(api),
                              FORGE_NATIVE_SDK_ABI,
                              FORGE_NATIVE_SDK_FINGERPRINT,
                              "project.navigation_probe",
                              "1",
                              deps,
                              2,
                              FORGE_SDK_RUNTIME,
                              FORGE_SDK_RUNTIME,
#ifdef FORGE_NAVIGATION_DENIED
                              0,
                              FORGE_SDK_DIAGNOSTICS,
#else
                              FORGE_SDK_NAVIGATION,
                              FORGE_SDK_NAVIGATION | FORGE_SDK_DIAGNOSTICS,
#endif
                              &ecs_init,
                              &ecs_os_api,
                              schemas,
                              start,
                              nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
