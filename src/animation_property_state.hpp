#pragma once
#include "audio_values.hpp"
#include "builtins.hpp"
#include "render_values.hpp"
#include <flecs.h>
#include <forge/animation_property.hpp>
#include <forge/render_view.hpp>
#include <forge/transform_animation.hpp>
#include <optional>
namespace forge {
// Typed candidates inside the current animation owner. No component creation.
struct AnimationPropertyState {
    std::optional<Camera> camera;
    std::optional<Light> light;
    std::optional<AudioSource> audio;
    std::optional<ParticleEmitter> particle;
    std::optional<SpriteRenderer> sprite;
    static constexpr unsigned camera_mask = 3u, light_mask = 60u, audio_mask = 192u;
    static constexpr unsigned particle_mask =
        animation_property_bit(AnimationProperty::ParticleSize) |
        animation_property_bit(AnimationProperty::ParticleRed) |
        animation_property_bit(AnimationProperty::ParticleGreen) |
        animation_property_bit(AnimationProperty::ParticleBlue);
    static constexpr unsigned sprite_mask = animation_property_bit(AnimationProperty::SpriteFrame);
    static bool available(flecs::entity entity, AnimationProperty property) {
        const auto bit = animation_property_bit(property);
        return entity && ((bit & camera_mask)     ? entity.has<Camera>()
                          : (bit & light_mask)    ? entity.has<Light>()
                          : (bit & audio_mask)    ? entity.has<AudioSource>()
                          : (bit & particle_mask) ? entity.has<ParticleEmitter>()
                          : (bit & sprite_mask)   ? entity.has<SpriteRenderer>()
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
        if (mask & particle_mask) {
            if (!entity.has<ParticleEmitter>())
                throw std::runtime_error(
                    "Animated particle property requires a ParticleEmitter component");
            result.particle = entity.get<ParticleEmitter>();
            detail::validate_reflected_value(entity, *result.particle);
        }
        if (mask & sprite_mask) {
            if (!entity.has<SpriteRenderer>())
                throw std::runtime_error("Animated frame requires a SpriteRenderer component");
            result.sprite = entity.get<SpriteRenderer>();
            detail::validate_sprite_renderer(*result.sprite);
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
        if (mask & particle_mask) {
            if (!components.contains("forge.particle_emitter"))
                throw std::runtime_error(
                    "Animated particle property requires a ParticleEmitter component");
            const auto& value = components.at("forge.particle_emitter");
            detail::validate_components({{"forge.particle_emitter", value}});
            result.particle = detail::particle_emitter_value(value);
        }
        if (mask & sprite_mask) {
            if (!components.contains("forge.sprite_renderer"))
                throw std::runtime_error("Animated frame requires a SpriteRenderer component");
            result.sprite = detail::sprite_renderer_value(components.at("forge.sprite_renderer"));
        }
        return result;
    }
    // Whole-clip, target-dependent admission. The fixed sampler only validates
    // its current candidate, never scans every key each tick.
    void validate_clip(const TransformAnimationClip& clip) const {
        if (!(clip.properties & sprite_mask))
            return;
        const auto count = std::uint64_t(sprite.value().columns) * sprite.value().rows;
        for (const auto& key : clip.keys)
            if (key.properties[unsigned(AnimationProperty::SpriteFrame)] >= double(count))
                throw std::runtime_error("Animated frame exceeds the target sprite grid");
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
        case AnimationProperty::ParticleSize:
            return particle.value().size_multiplier;
        case AnimationProperty::ParticleRed:
            return particle.value().tint_r;
        case AnimationProperty::ParticleGreen:
            return particle.value().tint_g;
        case AnimationProperty::ParticleBlue:
            return particle.value().tint_b;
        case AnimationProperty::SpriteFrame:
            return sprite.value().frame;
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
            case AnimationProperty::ParticleSize:
                particle.value().size_multiplier = v;
                break;
            case AnimationProperty::ParticleRed:
                particle.value().tint_r = float(v);
                break;
            case AnimationProperty::ParticleGreen:
                particle.value().tint_g = float(v);
                break;
            case AnimationProperty::ParticleBlue:
                particle.value().tint_b = float(v);
                break;
            case AnimationProperty::SpriteFrame:
                sprite.value().frame = std::uint32_t(v);
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
        if (sprite)
            detail::validate_sprite_renderer(*sprite);
        // Particle candidates preflight all native fields before sampling. Only
        // the four appearance scalars change, and each uses the same native range.
        // Avoid JSON construction/validation on the fixed animation path.
    }
    void commit(flecs::entity entity) const {
        if (camera)
            entity.set<Camera>(*camera);
        if (light)
            entity.set<Light>(*light);
        if (audio)
            entity.set<AudioSource>(*audio);
        if (particle)
            entity.set<ParticleEmitter>(*particle);
        if (sprite)
            entity.set<SpriteRenderer>(*sprite);
    }
    nlohmann::json patch(unsigned mask) const {
        auto result = nlohmann::json::object();
        for (const auto& binding : animation_property_bindings())
            if (mask & animation_property_bit(binding.property)) {
                if (binding.property == AnimationProperty::SpriteFrame)
                    result[binding.component][binding.field] = sprite.value().frame;
                else
                    result[binding.component][binding.field] = value(binding.property);
            }
        return result;
    }
};
} // namespace forge
