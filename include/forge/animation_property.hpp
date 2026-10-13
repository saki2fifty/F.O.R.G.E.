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
    AudioPitch
};
inline constexpr unsigned animation_property_count = 8;
using AnimationPropertyValues = std::array<double, animation_property_count>;
struct AnimationPropertyBinding {
    AnimationProperty property;
    const char* name;
    const char* component;
    const char* field;
    const char* label;
    double display_multiplier;
};
std::span<const AnimationPropertyBinding> animation_property_bindings();
const AnimationPropertyBinding& animation_property_binding(AnimationProperty);
AnimationProperty animation_property_named(std::string_view);
void validate_animation_property(AnimationProperty, double);
constexpr unsigned animation_property_bit(AnimationProperty property) {
    return 1u << unsigned(property);
}
} // namespace forge
