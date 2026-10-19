#include <algorithm>
#include <cmath>
#include <forge/animation_property.hpp>
#include <limits>
#include <numbers>
#include <stdexcept>
namespace forge {
std::span<const AnimationPropertyBinding> animation_property_bindings() {
    static constexpr std::array bindings{
        AnimationPropertyBinding{AnimationProperty::CameraFov, "forge.camera.vertical_fov",
                                 "forge.camera", "vertical_fov", "Camera field of view (degrees)",
                                 180 / std::numbers::pi},
        AnimationPropertyBinding{AnimationProperty::CameraHeight,
                                 "forge.camera.orthographic_height", "forge.camera",
                                 "orthographic_height", "Camera orthographic height", 1},
        AnimationPropertyBinding{AnimationProperty::LightIntensity, "forge.light.intensity",
                                 "forge.light", "intensity", "Light intensity", 1},
        AnimationPropertyBinding{AnimationProperty::LightRed, "forge.light.color_r", "forge.light",
                                 "color_r", "Light red (linear)", 1},
        AnimationPropertyBinding{AnimationProperty::LightGreen, "forge.light.color_g",
                                 "forge.light", "color_g", "Light green (linear)", 1},
        AnimationPropertyBinding{AnimationProperty::LightBlue, "forge.light.color_b", "forge.light",
                                 "color_b", "Light blue (linear)", 1},
        AnimationPropertyBinding{AnimationProperty::AudioGain, "forge.audio_source.gain",
                                 "forge.audio_source", "gain", "Audio volume (linear)", 1},
        AnimationPropertyBinding{AnimationProperty::AudioPitch, "forge.audio_source.pitch",
                                 "forge.audio_source", "pitch", "Audio pitch / speed", 1},
        AnimationPropertyBinding{AnimationProperty::ParticleSize,
                                 "forge.particle_emitter.size_multiplier", "forge.particle_emitter",
                                 "size_multiplier", "Particle size multiplier", 1},
        AnimationPropertyBinding{AnimationProperty::ParticleRed, "forge.particle_emitter.tint_r",
                                 "forge.particle_emitter", "tint_r", "Particle red (linear)", 1},
        AnimationPropertyBinding{AnimationProperty::ParticleGreen, "forge.particle_emitter.tint_g",
                                 "forge.particle_emitter", "tint_g", "Particle green (linear)", 1},
        AnimationPropertyBinding{AnimationProperty::ParticleBlue, "forge.particle_emitter.tint_b",
                                 "forge.particle_emitter", "tint_b", "Particle blue (linear)", 1},
        AnimationPropertyBinding{AnimationProperty::SpriteFrame, "forge.sprite_renderer.frame",
                                 "forge.sprite_renderer", "frame", "Sprite frame (step)", 1}};
    return bindings;
}
const AnimationPropertyBinding& animation_property_binding(AnimationProperty property) {
    if (unsigned(property) >= animation_property_count)
        throw std::runtime_error("Unknown animated property");
    return animation_property_bindings()[unsigned(property)];
}
AnimationProperty animation_property_named(std::string_view name) {
    for (const auto& binding : animation_property_bindings())
        if (binding.name == name)
            return binding.property;
    throw std::runtime_error("Unsupported animated component property");
}
bool animation_property_discrete(AnimationProperty property) {
    (void)animation_property_binding(property);
    return property == AnimationProperty::SpriteFrame;
}
AnimationPropertyLimits animation_property_limits(AnimationProperty property) {
    (void)animation_property_binding(property);
    switch (property) {
    case AnimationProperty::CameraFov:
        return {0, std::numbers::pi, true, true};
    case AnimationProperty::CameraHeight:
        return {0, 1e9, true};
    case AnimationProperty::LightIntensity:
        return {0, 1e9};
    case AnimationProperty::AudioGain:
        return {0, 4};
    case AnimationProperty::AudioPitch:
        return {.25, 4};
    case AnimationProperty::ParticleSize:
        return {.01, 16};
    case AnimationProperty::SpriteFrame:
        return {0, 16384. * 16384 - 1};
    default:
        return {0, 1};
    }
}
void validate_animation_property(AnimationProperty property, double value) {
    const auto limits = animation_property_limits(property);
    const bool admitted =
        std::isfinite(value) &&
        (limits.minimum_exclusive ? value > limits.minimum : value >= limits.minimum) &&
        (limits.maximum_exclusive ? value < limits.maximum : value <= limits.maximum);
    if (!admitted || (animation_property_discrete(property) && std::trunc(value) != value))
        throw std::runtime_error(std::string(animation_property_binding(property).label) +
                                 " is outside the animated value range");
}
double animation_property_curve_sample(AnimationProperty property, double value) {
    if (animation_property_discrete(property))
        throw std::runtime_error("Discrete animated properties do not have curve samples");
    const auto limits = animation_property_limits(property);
    const auto low =
        limits.minimum_exclusive ? std::nextafter(limits.minimum, limits.maximum) : limits.minimum;
    const auto high =
        limits.maximum_exclusive ? std::nextafter(limits.maximum, limits.minimum) : limits.maximum;
    const auto tolerance = 64 * std::numeric_limits<double>::epsilon() *
                           std::max({1., std::abs(limits.minimum), std::abs(limits.maximum)});
    if (!std::isfinite(value) || value < low - tolerance || value > high + tolerance)
        throw std::runtime_error("Animated curve sample is outside its admitted profile");
    return std::clamp(value, low, high);
}
} // namespace forge
