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
            int32_t count;
            double nearest = -1;
            if (data.planar) {
                std::array<ForgeSdkPhysicsHit2DV1, FORGE_SDK_PHYSICS_RAY_HIT_LIMIT> hits{};
                count = physics.raycast_all_2d({data.origin_x, data.origin_y},
                                               {data.delta_x, data.delta_y}, UINT32_MAX,
                                               data.include_sensors, hits.data(), hits.size());
                if (count > 0)
                    nearest = hits[0].fraction;
            } else {
                std::array<ForgeSdkPhysicsHitV1, FORGE_SDK_PHYSICS_RAY_HIT_LIMIT> hits{};
                count = physics.raycast_all({data.origin_x, data.origin_y, data.origin_z},
                                            {data.delta_x, data.delta_y, data.delta_z}, UINT32_MAX,
                                            data.include_sensors, hits.data(), hits.size());
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
