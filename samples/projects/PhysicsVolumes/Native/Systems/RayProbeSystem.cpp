#include "../Components/RayExclusion.hpp"
#include "../Components/RayProbe.hpp"
#include <array>
#include <forge/sdk_client.hpp>
#include <stdexcept>

void forge_register_system_RayProbeSystem(const ForgeSdkWorldV1* host) {
    flecs::world(host->world)
        .system<RayProbe>("gameplay.RayObserve")
        .kind(host->post_physics_phase)
        .each([host](RayProbe& data) {
            forge::sdk::Client physics(host);
            std::array<ForgeSdkEntityRefV1, FORGE_SDK_PHYSICS_EXCLUSION_LIMIT> ignored;
            std::size_t ignored_count = 0;
            bool reference_failed = false, overflow = false;
            flecs::world(host->world)
                .each<const RayExclusion>([&](flecs::entity entity, const RayExclusion& marker) {
                    if (!marker.enabled || reference_failed || overflow)
                        return;
                    ForgeSdkEntityRefV1 ref{};
                    ref.size = sizeof(ref);
                    const auto status = host->entity_reference(host->context, entity.id(), &ref);
                    if (status < 0) {
                        reference_failed = true;
                        return;
                    }
                    if (!status)
                        return; // A transient unregistered entity has no physical identity.
                    if (ignored_count == ignored.size()) {
                        overflow = true;
                        return;
                    }
                    ignored[ignored_count++] = ref;
                });
            // Release the nested Flecs iterator before reporting a failure.
            if (reference_failed)
                throw std::runtime_error("Ray exclusion identity read rejected");
            if (overflow)
                throw std::runtime_error("This ray example supports at most 16 excluded objects");
            const std::span<const ForgeSdkEntityRefV1> excluded(ignored.data(), ignored_count);
            int32_t count;
            double nearest = -1;
            if (data.planar) {
                std::array<ForgeSdkPhysicsHit2DV1, FORGE_SDK_PHYSICS_RAY_HIT_LIMIT> hits{};
                count = physics.raycast_all_2d_excluding(
                    {data.origin_x, data.origin_y}, {data.delta_x, data.delta_y}, UINT32_MAX,
                    data.include_sensors, excluded, hits.data(), hits.size());
                if (count > 0)
                    nearest = hits[0].fraction;
            } else {
                std::array<ForgeSdkPhysicsHitV1, FORGE_SDK_PHYSICS_RAY_HIT_LIMIT> hits{};
                count = physics.raycast_all_excluding({data.origin_x, data.origin_y, data.origin_z},
                                                      {data.delta_x, data.delta_y, data.delta_z},
                                                      UINT32_MAX, data.include_sensors, excluded,
                                                      hits.data(), hits.size());
                if (count > 0)
                    nearest = hits[0].fraction;
            }
            if (count < 0)
                throw std::runtime_error("Ray query invalid or complete result budget exceeded");
            if (!data.observed || data.last_count != count) {
                char text[96];
                std::snprintf(text, sizeof(text), "Ray query %s: count %d",
                              data.planar ? "XY" : "3D", count);
                if (host->diagnostic(host->context, 1, text) != 1)
                    throw std::runtime_error("Ray query diagnostic rejected");
            }
            data.observed = true;
            data.last_count = count;
            data.nearest_fraction = nearest;
        })
        .add(host->fixed_tag);
}
