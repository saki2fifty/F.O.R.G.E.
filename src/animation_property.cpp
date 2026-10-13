#include <cmath>
#include <forge/animation_property.hpp>
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
                                 "color_b", "Light blue (linear)", 1}};
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
void validate_animation_property(AnimationProperty property, double value) {
    (void)animation_property_binding(property);
    const bool admitted =
        std::isfinite(value) &&
        (property == AnimationProperty::CameraFov        ? value > 0 && value < std::numbers::pi
         : property == AnimationProperty::CameraHeight   ? value > 0 && value <= 1e9
         : property == AnimationProperty::LightIntensity ? value >= 0 && value <= 1e9
                                                         : value >= 0 && value <= 1);
    if (!admitted)
        throw std::runtime_error(std::string(animation_property_binding(property).label) +
                                 " is outside the animated value range");
}
} // namespace forge
