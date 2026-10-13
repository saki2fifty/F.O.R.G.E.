#pragma once
#include "render_values.hpp"
#include <flecs.h>
#include <forge/animation_property.hpp>
#include <forge/render_view.hpp>
#include <optional>
namespace forge {
// Typed candidates inside the current animation owner. No component creation.
struct AnimationPropertyState {
    std::optional<Camera> camera;
    std::optional<Light> light;
    static AnimationPropertyState from(flecs::entity entity, unsigned mask) {
        AnimationPropertyState result;
        if (mask & 3u) {
            if (!entity.has<Camera>())
                throw std::runtime_error("Animated camera property requires a Camera component");
            result.camera = entity.get<Camera>();
        }
        if (mask & ~3u) {
            if (!entity.has<Light>())
                throw std::runtime_error("Animated light property requires a Light component");
            result.light = entity.get<Light>();
        }
        return result;
    }
    static AnimationPropertyState from(const nlohmann::json& components, unsigned mask) {
        AnimationPropertyState result;
        if (mask & 3u) {
            if (!components.contains("forge.camera"))
                throw std::runtime_error("Animated camera property requires a Camera component");
            result.camera = detail::camera_value(components.at("forge.camera"));
        }
        if (mask & ~3u) {
            if (!components.contains("forge.light"))
                throw std::runtime_error("Animated light property requires a Light component");
            result.light = detail::light_value(components.at("forge.light"));
        }
        return result;
    }
    double value(AnimationProperty property) const {
        switch (property) {
        case AnimationProperty::CameraFov:
            return camera.value().vertical_fov;
        case AnimationProperty::CameraHeight:
            return camera.value().orthographic_height;
        case AnimationProperty::LightIntensity:
            return light.value().intensity;
        case AnimationProperty::LightRed:
            return light.value().color_r;
        case AnimationProperty::LightGreen:
            return light.value().color_g;
        case AnimationProperty::LightBlue:
            return light.value().color_b;
        }
        throw std::runtime_error("Unknown animated property");
    }
    void sample(unsigned mask, const AnimationPropertyValues& values) {
        for (const auto& binding : animation_property_bindings()) {
            if (!(mask & animation_property_bit(binding.property)))
                continue;
            const auto v = values[unsigned(binding.property)];
            validate_animation_property(binding.property, v);
            switch (binding.property) {
            case AnimationProperty::CameraFov:
                camera.value().vertical_fov = v;
                break;
            case AnimationProperty::CameraHeight:
                camera.value().orthographic_height = v;
                break;
            case AnimationProperty::LightIntensity:
                light.value().intensity = v;
                break;
            case AnimationProperty::LightRed:
                light.value().color_r = float(v);
                break;
            case AnimationProperty::LightGreen:
                light.value().color_g = float(v);
                break;
            case AnimationProperty::LightBlue:
                light.value().color_b = float(v);
                break;
            }
        }
        if (camera)
            validate_camera(*camera);
        if (light)
            validate_light(*light);
    }
    void commit(flecs::entity entity) const {
        if (camera)
            entity.set<Camera>(*camera);
        if (light)
            entity.set<Light>(*light);
    }
    nlohmann::json patch(unsigned mask) const {
        auto result = nlohmann::json::object();
        for (const auto& binding : animation_property_bindings())
            if (mask & animation_property_bit(binding.property))
                result[binding.component][binding.field] = value(binding.property);
        return result;
    }
};
} // namespace forge
