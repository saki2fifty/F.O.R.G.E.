#include "../Components/AnimatedSpin.hpp"
#include <cmath>
#include <forge/transform_components.hpp>
#include <stdexcept>

void forge_register_system_AnimatedSpinSystem(const ForgeSdkWorldV1* host) {
    if (!host->post_animation_phase || !host->fixed_tag)
        throw std::runtime_error("Animation phases unavailable");
    flecs::world world(host->world);
    // The existing post-animation phase reads this tick's committed field sample.
    world.system<const AnimatedSpin>("gameplay.AnimatedSpinSystem")
        .kind(host->post_animation_phase)
        .each([](flecs::iter& it, size_t row, const AnimatedSpin& data) {
            if (!std::isfinite(data.settings.speed))
                throw std::runtime_error("Animated Spin speed must be finite");
            if (data.settings.direction != SpinDirection::Forward &&
                data.settings.direction != SpinDirection::Reverse)
                throw std::runtime_error("Animated Spin direction must be a declared choice");
            if (!data.settings.enabled)
                return;
            auto entity = it.entity(row);
            const auto q = entity.has<forge::LocalRotation>() ? entity.get<forge::LocalRotation>()
                                                              : forge::LocalRotation{};
            const double half = double(data.settings.speed) *
                                std::int32_t(data.settings.direction) * it.delta_time() *
                                0.008726646259971648;
            const double ax = data.settings.axis[0], ay = data.settings.axis[1],
                         az = data.settings.axis[2];
            const double axis_length = std::sqrt(ax * ax + ay * ay + az * az);
            if (!std::isfinite(axis_length) || axis_length <= 0)
                throw std::runtime_error("Animated Spin axis must be finite and nonzero");
            const double s = std::sin(half) / axis_length, c = std::cos(half);
            const double x = q.x * c + (q.w * ax + q.y * az - q.z * ay) * s;
            const double y = q.y * c + (q.w * ay + q.z * ax - q.x * az) * s;
            const double z = q.z * c + (q.w * az + q.x * ay - q.y * ax) * s;
            const double w = q.w * c - (q.x * ax + q.y * ay + q.z * az) * s;
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!std::isfinite(length) || length <= 0)
                throw std::runtime_error("Animated Spin rotation must remain normalized");
            entity.set<forge::LocalRotation>(
                {float(x / length), float(y / length), float(z / length), float(w / length)});
        })
        .add(host->fixed_tag);
}
