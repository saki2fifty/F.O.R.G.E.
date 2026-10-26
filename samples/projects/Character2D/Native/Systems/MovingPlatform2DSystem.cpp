#include "../Components/MovingPlatform2D.hpp"
#include <cmath>
#include <forge/sdk_client.hpp>
#include <stdexcept>

namespace {
struct PlatformClock {
    double elapsed = 0;
};
} // namespace
void forge_register_system_MovingPlatform2DSystem(const ForgeSdkWorldV1* host) {
    flecs::world world(host->world);
    world.component<PlatformClock>("gameplay.PlatformClock");
    world.system<const MovingPlatform2D>("gameplay.MovingPlatform2DSystem")
        .kind(host->fixed_phase)
        .each([host](flecs::iter& it, size_t row, const MovingPlatform2D& data) {
            if (!std::isfinite(data.start_x) || !std::isfinite(data.start_y) ||
                !std::isfinite(data.end_x) || !std::isfinite(data.end_y) ||
                !std::isfinite(data.duration) || data.duration < .1 || data.duration > 60 ||
                std::hypot(data.end_x - data.start_x, data.end_y - data.start_y) / data.duration >
                    20)
                throw std::runtime_error("Moving Platform 2D needs finite endpoints, "
                                         "duration0.1..60s and speed at most20m/s");
            auto& clock = it.entity(row).ensure<PlatformClock>();
            clock.elapsed = std::fmod(clock.elapsed + it.delta_time(), 2 * data.duration);
            const double phase = clock.elapsed / data.duration;
            const double fraction = phase <= 1 ? phase : 2 - phase;
            const double position[]{data.start_x + (data.end_x - data.start_x) * fraction,
                                    data.start_y + (data.end_y - data.start_y) * fraction};
            ForgeSdkEntityRefV1 ref{};
            ref.size = sizeof(ref);
            if (host->entity_reference(host->context, it.entity(row).id(), &ref) != 1 ||
                !forge::sdk::Client(host).move_2d(ref.scene_uuid, ref.entity_uuid, position, 0,
                                                  true))
                throw std::runtime_error("Moving Platform 2D needs an enabled Kinematic body");
        })
        .add(host->fixed_tag);
}
