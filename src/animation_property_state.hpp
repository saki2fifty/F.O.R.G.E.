#pragma once
#include "audio_values.hpp"
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
    std::optional<AudioSource> audio;
    static constexpr unsigned camera_mask = 3u, light_mask = 60u, audio_mask = 192u;
    static bool available(flecs::entity entity, AnimationProperty property) {
        const auto bit = animation_property_bit(property);
        return entity && ((bit & camera_mask)  ? entity.has<Camera>()
                          : (bit & light_mask) ? entity.has<Light>()
                          : (bit & audio_mask) ? entity.has<AudioSource>()
                                               : false);
    }
    static AnimationPropertyState from(flecs::entity entity, unsigned mask) {
        AnimationPropertyState result;
        if (mask & camera_mask) {
            if (!entity.has<Camera>())
                throw std::runtime_error("Animated camera property requires a Camera component");
            result.camera = entity.get<Camera>();
        }
        if (mask & light_mask) {
            if (!entity.has<Light>())
                throw std::runtime_error("Animated light property requires a Light component");
            result.light = entity.get<Light>();
        }
        if (mask & audio_mask) {
            if (!entity.has<AudioSource>())
                throw std::runtime_error(
                    "Animated audio property requires an AudioSource component");
            result.audio = entity.get<AudioSource>();
            detail::validate_audio_source(entity, *result.audio);
        }
        return result;
    }
    static AnimationPropertyState from(const nlohmann::json& components, unsigned mask) {
        AnimationPropertyState result;
        if (mask & camera_mask) {
            if (!components.contains("forge.camera"))
                throw std::runtime_error("Animated camera property requires a Camera component");
            result.camera = detail::camera_value(components.at("forge.camera"));
        }
        if (mask & light_mask) {
            if (!components.contains("forge.light"))
                throw std::runtime_error("Animated light property requires a Light component");
            result.light = detail::light_value(components.at("forge.light"));
        }
        if (mask & audio_mask) {
            if (!components.contains("forge.audio_source"))
                throw std::runtime_error(
                    "Animated audio property requires an AudioSource component");
            result.audio = detail::audio_source_value(components.at("forge.audio_source"));
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
        case AnimationProperty::AudioGain:
            return audio.value().gain;
        case AnimationProperty::AudioPitch:
            return audio.value().pitch;
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
            case AnimationProperty::AudioGain:
                audio.value().gain = float(v);
                break;
            case AnimationProperty::AudioPitch:
                audio.value().pitch = float(v);
                break;
            }
        }
        if (camera)
            validate_camera(*camera);
        if (light)
            validate_light(*light);
        // All untouched audio fields were validated when copying the candidate;
        // the only changed scalars have the same admitted gain/pitch ranges.
        if (audio)
            detail::validate_audio_source(*audio);
    }
    void commit(flecs::entity entity) const {
        if (camera)
            entity.set<Camera>(*camera);
        if (light)
            entity.set<Light>(*light);
        if (audio)
            entity.set<AudioSource>(*audio);
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
