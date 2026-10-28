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
            if (!std::isfinite(data.speed))
                throw std::runtime_error("Animated Spin speed must be finite");
            if (data.direction != SpinDirection::Forward &&
                data.direction != SpinDirection::Reverse)
                throw std::runtime_error("Animated Spin direction must be a declared choice");
            if (!data.enabled)
                return;
            auto entity = it.entity(row);
            const auto q = entity.has<forge::LocalRotation>() ? entity.get<forge::LocalRotation>()
                                                              : forge::LocalRotation{};
            const double half = double(data.speed) * std::int32_t(data.direction) *
                                it.delta_time() * 0.008726646259971648;
            const double s = std::sin(half), c = std::cos(half);
            const double x = q.x * c - q.z * s, y = q.y * c + q.w * s;
            const double z = q.z * c + q.x * s, w = q.w * c - q.y * s;
            const double length = std::sqrt(x * x + y * y + z * z + w * w);
            if (!std::isfinite(length) || length <= 0)
                throw std::runtime_error("Animated Spin rotation must remain normalized");
            entity.set<forge::LocalRotation>(
                {float(x / length), float(y / length), float(z / length), float(w / length)});
        })
        .add(host->fixed_tag);
}
