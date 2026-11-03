#include <cmath>
#include <forge/scene_render_settings.hpp>
namespace forge {
void validate_scene_screen_space_reflections(const SceneScreenSpaceReflections& value) {
    if (!std::isfinite(value.max_distance) || value.max_distance < 0 || value.max_distance > 100 ||
        !std::isfinite(value.thickness) || value.thickness < .0001f || value.thickness > 1 ||
        !std::isfinite(value.strength) || value.strength < 0 || value.strength > 1 ||
        !std::isfinite(value.max_roughness) || value.max_roughness < 0 || value.max_roughness > 1)
        throw std::runtime_error(
            "Screen-space reflections require distance from 0 to 100 metres, "
            "thickness from 0.0001 to 1 metre and strength/roughness from 0 to 1");
}
void validate_scene_ambient_occlusion(const SceneAmbientOcclusion& value) {
    if (!std::isfinite(value.radius) || value.radius < 0 || value.radius > 10 ||
        !std::isfinite(value.strength) || value.strength < 0 || value.strength > 1 ||
        !std::isfinite(value.bias) || value.bias < 0 || value.bias > 1 ||
        !std::isfinite(value.normal_influence) || value.normal_influence < 0 ||
        value.normal_influence > 1)
        throw std::runtime_error(
            "Ambient occlusion requires radius from 0 to 10 metres, "
            "strength/normal influence from 0 to 1 and depth bias from 0 to 1 metre");
}
void validate_scene_motion_blur(const SceneMotionBlur& value) {
    if (!std::isfinite(value.shutter) || value.shutter < 0 || value.shutter > 1 ||
        !std::isfinite(value.max_pixels) || value.max_pixels < 0 || value.max_pixels > 32)
        throw std::runtime_error("Motion blur requires shutter fraction from 0 to 1 and maximum "
                                 "trail from 0 to 32 pixels");
}
void validate_scene_depth_of_field(const SceneDepthOfField& value) {
    if (!std::isfinite(value.focus_distance) || value.focus_distance < .001f ||
        value.focus_distance > 1000000 || !std::isfinite(value.focus_range) ||
        value.focus_range < .001f || value.focus_range > 1000000 ||
        !std::isfinite(value.blur_radius) || value.blur_radius < 0 || value.blur_radius > 12)
        throw std::runtime_error("Depth of field requires focus distance/range from 0.001 to "
                                 "1000000 metres and blur radius from 0 to 12 pixels");
}
void validate_scene_temporal_anti_aliasing(const SceneTemporalAntiAliasing& value) {
    if (!std::isfinite(value.stability) || value.stability < 0 || value.stability > .95f)
        throw std::runtime_error("Temporal anti-aliasing stability must be finite from 0 to 0.95");
}
void validate_scene_fast_approximate_anti_aliasing(const SceneFastApproximateAntiAliasing& value) {
    if (!std::isfinite(value.subpixel_strength) || value.subpixel_strength < 0 ||
        value.subpixel_strength > 1 || value.search_steps < 1 || value.search_steps > 16)
        throw std::runtime_error("Fast approximate anti-aliasing requires finite subpixel "
                                 "strength from 0 to 1 and 1 to 16 edge search steps");
}
void validate_scene_distance_fog(const SceneDistanceFog& value) {
    for (const auto channel : value.color)
        if (!std::isfinite(channel) || channel < 0 || channel > 65504)
            throw std::runtime_error(
                "Distance fog color requires finite linear RGB from 0 to 65504");
    if (!std::isfinite(value.start) || !std::isfinite(value.end) || value.start < 0 ||
        value.end < .001f || value.end > 1000000 || value.start >= value.end)
        throw std::runtime_error(
            "Distance fog requires start >= 0 and end from 0.001 to 1000000 metres, after start");
}
void validate_scene_auto_exposure(const SceneAutoExposure& value) {
    if (!std::isfinite(value.min_ev) || !std::isfinite(value.max_ev) || value.min_ev < -20 ||
        value.max_ev > 20 || value.min_ev > value.max_ev)
        throw std::runtime_error(
            "Automatic exposure needs ordered finite EV limits within -20 to +20");
    if (!std::isfinite(value.low_percentile) || !std::isfinite(value.high_percentile) ||
        value.low_percentile < 0 || value.high_percentile > 1 ||
        value.low_percentile >= value.high_percentile)
        throw std::runtime_error("Automatic exposure needs ordered percentiles within 0 to 1");
    if (!std::isfinite(value.target_gray) || value.target_gray < .001f || value.target_gray > 1 ||
        !std::isfinite(value.speed_up) || !std::isfinite(value.speed_down) ||
        value.speed_up < .01f || value.speed_up > 20 || value.speed_down < .01f ||
        value.speed_down > 20)
        throw std::runtime_error(
            "Automatic exposure needs target gray0.001–1 and adaptation rates0.01–20");
}
void validate_scene_color_adjustments(const SceneColorAdjustments& value) {
    for (const auto channel : value.filter)
        if (!std::isfinite(channel) || channel < 0 || channel > 4)
            throw std::runtime_error(
                "Color filter requires finite linear RGB channels from 0 to 4");
    if (!std::isfinite(value.saturation) || value.saturation < 0 || value.saturation > 2 ||
        !std::isfinite(value.contrast) || value.contrast < 0 || value.contrast > 2)
        throw std::runtime_error("Color saturation and contrast must be finite from 0 to 2");
}
void validate_scene_bloom(const SceneBloom& value) {
    if (!std::isfinite(value.threshold) || value.threshold < 0 || value.threshold > 65504 ||
        !std::isfinite(value.intensity) || value.intensity < 0 || value.intensity > 4 ||
        !std::isfinite(value.radius) || value.radius < .5f || value.radius > 16)
        throw std::runtime_error("Bloom requires threshold0–65504, intensity0–4 and radius0.5–16");
}
SceneRenderSettings scene_render_settings(const nlohmann::json& scene) {
    SceneRenderSettings result;
    if (!scene.contains("rendering"))
        return result;
    const auto& settings = scene.at("rendering");
    if (!settings.is_object() || !settings.at("version").is_number_unsigned() ||
        settings.at("version") != 1)
        throw std::runtime_error("Unsupported scene rendering settings version");
    if (settings.contains("weighted_transparency")) {
        if (!settings.at("weighted_transparency").is_boolean())
            throw std::runtime_error("Weighted transparency must be boolean");
        result.weighted_transparency = settings.at("weighted_transparency").get<bool>();
    }
    auto number = [](const auto& object, const char* key, double fallback) {
        if (!object.contains(key))
            return fallback;
        const auto& value = object.at(key);
        if (!value.is_number() || !std::isfinite(value.template get<double>()))
            throw std::runtime_error(
                std::string("Nonfinite or nonnumeric scene rendering value: ") + key);
        return value.template get<double>();
    };
    const auto ev = number(settings, "exposure", 0);
    if (ev < -20 || ev > 20)
        throw std::runtime_error("Game exposure must be between -20 and +20 stops");
    result.exposure = float(ev);
    if (settings.contains("screen_space_reflections")) {
        const auto& section = settings.at("screen_space_reflections");
        if (!section.is_object())
            throw std::runtime_error("Scene screen-space reflections must be an object");
        auto& value = result.screen_space_reflections;
        if (section.contains("enabled")) {
            if (!section.at("enabled").is_boolean())
                throw std::runtime_error("Screen-space reflections enabled must be boolean");
            value.enabled = section.at("enabled").get<bool>();
        }
        if (section.contains("roughness_filtering")) {
            if (!section.at("roughness_filtering").is_boolean())
                throw std::runtime_error("Reflection roughness filtering must be boolean");
            value.roughness_filtering = section.at("roughness_filtering").get<bool>();
        }
        const auto distance = number(section, "max_distance", value.max_distance);
        const auto thickness = number(section, "thickness", value.thickness);
        const auto strength = number(section, "strength", value.strength);
        const auto roughness = number(section, "max_roughness", value.max_roughness);
        if (distance < 0 || distance > 100 || thickness < .0001 || thickness > 1 || strength < 0 ||
            strength > 1 || roughness < 0 || roughness > 1)
            throw std::runtime_error("Screen-space reflection values are outside their ranges");
        value.max_distance = float(distance);
        value.thickness = float(thickness);
        value.strength = float(strength);
        value.max_roughness = float(roughness);
        validate_scene_screen_space_reflections(value);
    }
    if (settings.contains("ambient_occlusion")) {
        const auto& section = settings.at("ambient_occlusion");
        if (!section.is_object())
            throw std::runtime_error("Scene ambient occlusion must be an object");
        auto& value = result.ambient_occlusion;
        if (section.contains("enabled")) {
            if (!section.at("enabled").is_boolean())
                throw std::runtime_error("Ambient occlusion enabled must be boolean");
            value.enabled = section.at("enabled").get<bool>();
        }
        const auto radius = number(section, "radius", value.radius);
        const auto strength = number(section, "strength", value.strength);
        const auto bias = number(section, "bias", value.bias);
        const auto normal = number(section, "normal_influence", value.normal_influence);
        if (radius < 0 || radius > 10 || strength < 0 || strength > 1 || bias < 0 || bias > 1 ||
            normal < 0 || normal > 1)
            throw std::runtime_error("Ambient occlusion radius, strength, bias or normal influence "
                                     "is outside its range");
        value.radius = float(radius);
        value.strength = float(strength);
        value.bias = float(bias);
        value.normal_influence = float(normal);
        validate_scene_ambient_occlusion(value);
    }
    if (settings.contains("motion_blur")) {
        const auto& section = settings.at("motion_blur");
        if (!section.is_object())
            throw std::runtime_error("Scene motion blur must be an object");
        auto& value = result.motion_blur;
        if (section.contains("enabled")) {
            if (!section.at("enabled").is_boolean())
                throw std::runtime_error("Motion blur enabled must be boolean");
            value.enabled = section.at("enabled").get<bool>();
        }
        const auto shutter = number(section, "shutter", value.shutter);
        const auto pixels = number(section, "max_pixels", value.max_pixels);
        if (shutter < 0 || shutter > 1 || pixels < 0 || pixels > 32)
            throw std::runtime_error("Motion blur shutter or maximum trail is outside its range");
        value.shutter = float(shutter);
        value.max_pixels = float(pixels);
        validate_scene_motion_blur(value);
    }
    if (settings.contains("depth_of_field")) {
        const auto& section = settings.at("depth_of_field");
        if (!section.is_object())
            throw std::runtime_error("Scene depth of field must be an object");
        auto& value = result.depth_of_field;
        if (section.contains("enabled")) {
            if (!section.at("enabled").is_boolean())
                throw std::runtime_error("Depth of field enabled must be boolean");
            value.enabled = section.at("enabled").get<bool>();
        }
        const auto read_value = [&](const char* key, float initial, double low, double high) {
            const auto result = number(section, key, initial);
            if (result < low || result > high)
                throw std::runtime_error(std::string("Depth of field ") + key +
                                         " is outside its supported range");
            return float(result);
        };
        value.focus_distance = read_value("focus_distance", value.focus_distance, .001, 1000000);
        value.focus_range = read_value("focus_range", value.focus_range, .001, 1000000);
        value.blur_radius = read_value("blur_radius", value.blur_radius, 0, 12);
        validate_scene_depth_of_field(value);
    }
    if (settings.contains("fast_approximate_anti_aliasing")) {
        const auto& section = settings.at("fast_approximate_anti_aliasing");
        if (!section.is_object())
            throw std::runtime_error("Scene fast approximate anti-aliasing must be an object");
        auto& value = result.fast_approximate_anti_aliasing;
        if (section.contains("enabled")) {
            if (!section.at("enabled").is_boolean())
                throw std::runtime_error("Fast approximate anti-aliasing enabled must be boolean");
            value.enabled = section.at("enabled").get<bool>();
        }
        const auto strength = number(section, "subpixel_strength", value.subpixel_strength);
        if (section.contains("search_steps") && !section.at("search_steps").is_number_integer())
            throw std::runtime_error("Fast approximate anti-aliasing search steps must be integer");
        const auto steps = number(section, "search_steps", value.search_steps);
        if (strength < 0 || strength > 1 || steps < 1 || steps > 16 || std::floor(steps) != steps)
            throw std::runtime_error("Fast approximate anti-aliasing strength or steps are "
                                     "outside their supported range");
        value.subpixel_strength = float(strength);
        value.search_steps = unsigned(steps);
        validate_scene_fast_approximate_anti_aliasing(value);
    }
    if (settings.contains("temporal_anti_aliasing")) {
        const auto& temporal = settings.at("temporal_anti_aliasing");
        if (!temporal.is_object())
            throw std::runtime_error("Scene temporal anti-aliasing must be an object");
        auto& value = result.temporal_anti_aliasing;
        if (temporal.contains("enabled")) {
            if (!temporal.at("enabled").is_boolean())
                throw std::runtime_error("Temporal anti-aliasing enabled must be boolean");
            value.enabled = temporal.at("enabled").get<bool>();
        }
        const auto stability = number(temporal, "stability", value.stability);
        if (stability < 0 || stability > .95)
            throw std::runtime_error("Temporal anti-aliasing stability must be from 0 to 0.95");
        value.stability = float(stability);
        validate_scene_temporal_anti_aliasing(value);
    }
    if (settings.contains("distance_fog")) {
        const auto& fog = settings.at("distance_fog");
        if (!fog.is_object())
            throw std::runtime_error("Scene distance fog must be an object");
        auto& value = result.distance_fog;
        if (fog.contains("enabled")) {
            if (!fog.at("enabled").is_boolean())
                throw std::runtime_error("Distance fog enabled must be boolean");
            value.enabled = fog.at("enabled").get<bool>();
        }
        if (fog.contains("color")) {
            const auto& color = fog.at("color");
            if (!color.is_array() || color.size() != 3)
                throw std::runtime_error("Distance fog color requires three linear RGB channels");
            for (unsigned i = 0; i < 3; ++i) {
                if (!color[i].is_number() || !std::isfinite(color[i].get<double>()) ||
                    color[i].get<double>() < 0 || color[i].get<double>() > 65504)
                    throw std::runtime_error("Distance fog color channels must be from 0 to 65504");
                value.color[i] = color[i].get<float>();
            }
        }
        const auto start = number(fog, "start", value.start);
        const auto end = number(fog, "end", value.end);
        if (start < 0 || end < .001 || end > 1000000 || start >= end)
            throw std::runtime_error("Distance fog start/end exceed their ordered metre range");
        value.start = float(start);
        value.end = float(end);
        validate_scene_distance_fog(value);
    }
    if (settings.contains("auto_exposure")) {
        const auto& automatic = settings.at("auto_exposure");
        if (!automatic.is_object())
            throw std::runtime_error("Scene automatic exposure must be an object");
        auto& value = result.auto_exposure;
        if (automatic.contains("enabled")) {
            if (!automatic.at("enabled").is_boolean())
                throw std::runtime_error("Automatic exposure enabled must be boolean");
            value.enabled = automatic.at("enabled").get<bool>();
        }
        const auto bounded = [&](const char* key, double fallback, double minimum, double maximum) {
            const auto candidate = number(automatic, key, fallback);
            if (candidate < minimum || candidate > maximum)
                throw std::runtime_error(
                    std::string("Automatic exposure value exceeds its range: ") + key);
            return candidate;
        };
        const auto minimum = bounded("min_ev", value.min_ev, -20, 20);
        const auto maximum = bounded("max_ev", value.max_ev, -20, 20);
        const auto low = bounded("low_percentile", value.low_percentile, 0, 1);
        const auto high = bounded("high_percentile", value.high_percentile, 0, 1);
        if (minimum > maximum || low >= high)
            throw std::runtime_error("Automatic exposure limits and percentiles must be ordered");
        value.min_ev = float(minimum);
        value.max_ev = float(maximum);
        value.low_percentile = float(low);
        value.high_percentile = float(high);
        value.target_gray = float(bounded("target_gray", value.target_gray, .001, 1));
        value.speed_up = float(bounded("speed_up", value.speed_up, .01, 20));
        value.speed_down = float(bounded("speed_down", value.speed_down, .01, 20));
        validate_scene_auto_exposure(value);
    }
    if (settings.contains("color_adjustments")) {
        const auto& color = settings.at("color_adjustments");
        if (!color.is_object())
            throw std::runtime_error("Scene color adjustments must be an object");
        if (color.contains("enabled")) {
            if (!color.at("enabled").is_boolean())
                throw std::runtime_error("Color adjustments enabled must be boolean");
            result.color_adjustments.enabled = color.at("enabled").get<bool>();
        }
        if (color.contains("filter")) {
            const auto& filter = color.at("filter");
            if (!filter.is_array() || filter.size() != 3)
                throw std::runtime_error("Color filter requires three linear RGB channels");
            for (unsigned i = 0; i < 3; ++i) {
                if (!filter[i].is_number() || !std::isfinite(filter[i].get<double>()) ||
                    filter[i].get<double>() < 0 || filter[i].get<double>() > 4)
                    throw std::runtime_error("Color filter channels must be finite from 0 to 4");
                result.color_adjustments.filter[i] = filter[i].get<float>();
            }
        }
        const auto saturation = number(color, "saturation", 1);
        const auto contrast = number(color, "contrast", 1);
        if (saturation < 0 || saturation > 2 || contrast < 0 || contrast > 2)
            throw std::runtime_error("Color saturation and contrast must be from 0 to 2");
        result.color_adjustments.saturation = float(saturation);
        result.color_adjustments.contrast = float(contrast);
    }
    if (settings.contains("bloom")) {
        const auto& bloom = settings.at("bloom");
        if (!bloom.is_object())
            throw std::runtime_error("Scene bloom must be an object");
        if (bloom.contains("enabled")) {
            if (!bloom.at("enabled").is_boolean())
                throw std::runtime_error("Bloom enabled must be boolean");
            result.bloom.enabled = bloom.at("enabled").get<bool>();
        }
        const auto threshold = number(bloom, "threshold", 1);
        const auto intensity = number(bloom, "intensity", .25);
        const auto radius = number(bloom, "radius", 2);
        if (threshold < 0 || threshold > 65504 || intensity < 0 || intensity > 4 || radius < .5 ||
            radius > 16)
            throw std::runtime_error("Bloom settings exceed the admitted spatial profile");
        result.bloom.threshold = float(threshold);
        result.bloom.intensity = float(intensity);
        result.bloom.radius = float(radius);
    }
    if (settings.contains("environment")) {
        const auto& env = settings.at("environment");
        if (!env.is_object())
            throw std::runtime_error("Scene environment must be an object");
        if (env.contains("texture") && !env.at("texture").is_null())
            result.environment.texture.id = env.at("texture").template get<AssetId>();
        const auto intensity = number(env, "intensity", 1);
        if (intensity < 0 || intensity > std::numeric_limits<float>::max())
            throw std::runtime_error("Environment intensity must be a finite nonnegative float");
        result.environment.intensity = float(intensity);
        result.environment.rotation = number(env, "rotation", 0);
        if (env.contains("sky")) {
            if (!env.at("sky").is_boolean())
                throw std::runtime_error("Environment sky must be boolean");
            result.environment.sky = env.at("sky").template get<bool>();
        }
    }
    if (settings.contains("shadows")) {
        const auto& shadows = settings.at("shadows");
        if (!shadows.is_object())
            throw std::runtime_error("Scene shadows must be an object");
        auto count = [&](const char* key, unsigned fallback, unsigned minimum, unsigned maximum) {
            if (!shadows.contains(key))
                return fallback;
            const auto& value = shadows.at(key);
            if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
                value.template get<double>() < minimum || value.template get<double>() > maximum)
                throw std::runtime_error(
                    std::string("Shadow setting exceeds the render profile: ") + key);
            return value.template get<unsigned>();
        };
        // Native 3x3 PCF plus texel snapping needs a four-texel total margin.
        result.shadows.resolution = count("resolution", result.shadows.resolution, 5, UINT32_MAX);
        result.shadows.cascades =
            count("cascades", result.shadows.cascades, 1, shadow_cascade_limit);
        result.shadows.max_lights =
            count("max_lights", result.shadows.max_lights, 1, shadow_light_limit);
        result.shadows.distance = number(shadows, "distance", result.shadows.distance);
        if (result.shadows.distance < std::numeric_limits<float>::min() ||
            result.shadows.distance > std::numeric_limits<float>::max())
            throw std::runtime_error(
                "Shadow distance must be a positive normal finite float for GPU shadow projection");
        if (shadows.contains("enabled")) {
            if (!shadows.at("enabled").is_boolean())
                throw std::runtime_error("Shadow enabled must be boolean");
            result.shadows.enabled = shadows.at("enabled").template get<bool>();
        }
    }
    return result;
}
} // namespace forge
