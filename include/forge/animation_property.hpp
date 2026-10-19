#pragma once
#include <array>
#include <span>
#include <string_view>
namespace forge {
enum class AnimationProperty : unsigned {
    CameraFov,
    CameraHeight,
    LightIntensity,
    LightRed,
    LightGreen,
    LightBlue,
    AudioGain,
    AudioPitch,
    ParticleSize,
    ParticleRed,
    ParticleGreen,
    ParticleBlue,
    SpriteFrame
};
inline constexpr unsigned animation_property_count = 13;
using AnimationPropertyValues = std::array<double, animation_property_count>;
struct AnimationPropertyBinding {
    AnimationProperty property;
    const char* name;
    const char* component;
    const char* field;
    const char* label;
    double display_multiplier;
};
struct AnimationPropertyLimits {
    double minimum, maximum;
    bool minimum_exclusive = false, maximum_exclusive = false;
};
std::span<const AnimationPropertyBinding> animation_property_bindings();
const AnimationPropertyBinding& animation_property_binding(AnimationProperty);
AnimationProperty animation_property_named(std::string_view);
// Discrete bindings hold the preceding key until the next key's exact time.
// They have no linear/cubic tangent representation.
bool animation_property_discrete(AnimationProperty);
void validate_animation_property(AnimationProperty, double);
AnimationPropertyLimits animation_property_limits(AnimationProperty);
// Source admission has already proved the whole cubic interval. Correct only
// machine-precision excursions at its typed boundary; larger errors reject.
double animation_property_curve_sample(AnimationProperty, double);
constexpr unsigned animation_property_bit(AnimationProperty property) {
    return 1u << unsigned(property);
}
} // namespace forge
