#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <flecs.h>
#include <forge/native_sdk.h>
#include <forge/native_sdk_identity.h>
#include <forge/navigation_components.hpp>
#include <forge/transform_components.hpp>
#include <memory>
#include <tuple>

namespace {
int32_t fail(char* error, uint32_t capacity, const char* message) {
    if (error && capacity)
        std::snprintf(error, capacity, "%s", message);
    return 0;
}
int32_t FORGE_SDK_CALL schemas(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world.component<forge::NavigationAgent>("forge.navigation_agent");
        world.component<forge::LocalTranslation>("forge.local_translation");
        const auto type = world.component<forge::NavigationAvoidance>("forge.navigation_avoidance");
        const auto* info = ecs_get_type_info(world.c_ptr(), type.id());
        if (!info || info->size != sizeof(forge::NavigationAvoidance) ||
            info->alignment != alignof(forge::NavigationAvoidance))
            return fail(error, capacity, "Avoidance policy layout differs from the matching SDK");
        for (const auto& [field, offset, native] :
             {std::tuple{"layers", offsetof(forge::NavigationAvoidance, layers),
                         world.id<std::uint32_t>()},
              std::tuple{"mask", offsetof(forge::NavigationAvoidance, mask),
                         world.id<std::uint32_t>()},
              std::tuple{"priority", offsetof(forge::NavigationAvoidance, priority),
                         world.id<float>()}}) {
            const auto* member = ecs_struct_get_member(world.c_ptr(), type.id(), field);
            if (!member || std::size_t(member->offset) != offset || member->type != native)
                return fail(error, capacity, "Avoidance policy native member metadata differs");
        }
        auto plain = world.entity().add(flecs::Prefab).add<forge::NavigationAvoidance>();
        const bool defaults =
            plain.get<forge::NavigationAvoidance>() == forge::NavigationAvoidance{};
        plain.destruct();
        forge::NavigationAvoidance expected;
        expected.layers = expected.mask = 0x80000000u;
        expected.priority = .875f;
        auto first = world.entity().add(flecs::Prefab).set<forge::NavigationAvoidance>(expected);
        auto copy = world.entity()
                        .add(flecs::Prefab)
                        .set<forge::NavigationAvoidance>(first.get<forge::NavigationAvoidance>());
        first.destruct();
        const bool copied = copy.get<forge::NavigationAvoidance>() == expected;
        copy.destruct();
        return defaults && copied
                   ? 1
                   : fail(error, capacity, "Avoidance policy default/copy hooks differ");
    } catch (const std::exception& e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "Avoidance policy schema registration failed");
    }
}
struct Observation {
    std::array<double, 3> first{}, second{};
    bool have_first = false, have_second = false, planar = false, reported = false;
    double minimum_gap = 100, deviation = 0, yielding_deviation = 0;
};
int32_t FORGE_SDK_CALL start(const ForgeSdkWorldV1* host, char* error, uint32_t capacity) {
    try {
        flecs::world world(host->world);
        world
            .system<const forge::NavigationAgent, const forge::LocalTranslation,
                    const forge::NavigationAvoidance>()
            .kind(host->fixed_phase)
            .each([host, state = std::make_shared<Observation>()](
                      const forge::NavigationAgent&, const forge::LocalTranslation& position,
                      const forge::NavigationAvoidance& policy) {
                if (state->reported)
                    return;
                if (policy.layers != 0x80000000u || policy.mask != 0x80000000u ||
                    (policy.priority != .8f && policy.priority != .2f)) {
                    host->diagnostic(host->context, 3, "Avoidance policy SDK failed");
                    state->reported = true;
                    return;
                }
                const std::array<double, 3> p{position.x, position.y, position.z};
                if (policy.priority == .8f) {
                    if (!state->have_first)
                        state->planar = position.z == 2;
                    state->first = p;
                    state->have_first = true;
                } else {
                    state->second = p;
                    state->have_second = true;
                }
                if (!state->have_first || !state->have_second)
                    return;
                const auto axis = state->planar ? 1u : 2u;
                const auto baseline = state->planar ? .1 : 0;
                state->minimum_gap = std::min(state->minimum_gap,
                                              std::hypot(state->first[0] - state->second[0],
                                                         state->first[axis] - state->second[axis]));
                state->deviation =
                    std::max(state->deviation, std::abs(state->first[axis] - baseline));
                state->yielding_deviation =
                    std::max(state->yielding_deviation, std::abs(state->second[axis] - baseline));
                if (state->first[0] > 3.8 && state->second[0] < -3.8) {
                    const bool depth =
                        !state->planar || (state->first[2] == 2 && state->second[2] == 7);
                    const bool valid = depth && state->minimum_gap > .65 &&
                                       state->deviation < .001 && state->yielding_deviation > .2;
                    host->diagnostic(host->context, valid ? 1 : 3,
                                     valid ? "Avoidance policy SDK passed"
                                           : "Avoidance policy SDK failed");
                    state->reported = true;
                }
            })
            .add(host->fixed_tag);
        return 1;
    } catch (const std::exception& e) {
        return fail(error, capacity, e.what());
    } catch (...) {
        return fail(error, capacity, "Avoidance policy gameplay registration failed");
    }
}
const char* dependencies[]{"forge.navigation", "forge.transforms", "forge.input"};
const ForgeNativeSdkV1 api{sizeof(ForgeNativeSdkV1),
                           FORGE_NATIVE_SDK_ABI,
                           FORGE_NATIVE_SDK_FINGERPRINT,
                           "project.avoidance_policy_probe",
                           "1",
                           dependencies,
                           3,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_RUNTIME,
                           FORGE_SDK_NAVIGATION | FORGE_SDK_DIAGNOSTICS,
                           FORGE_SDK_NAVIGATION | FORGE_SDK_DIAGNOSTICS,
                           &ecs_init,
                           &ecs_os_api,
                           schemas,
                           start,
                           nullptr};
} // namespace
extern "C" FORGE_SDK_EXPORT const ForgeNativeSdkV1* FORGE_SDK_CALL forge_native_sdk_v1() {
    return &api;
}
