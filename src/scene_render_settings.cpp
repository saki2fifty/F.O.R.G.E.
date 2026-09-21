#include <cmath>
#include <forge/scene_render_settings.hpp>
namespace forge {
SceneRenderSettings scene_render_settings(const nlohmann::json& scene) {
    SceneRenderSettings result;
    if (!scene.contains("rendering"))
        return result;
    const auto& settings = scene.at("rendering");
    if (!settings.is_object() || !settings.at("version").is_number_unsigned() ||
        settings.at("version") != 1)
        throw std::runtime_error("Unsupported scene rendering settings version");
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
    return result;
}
} // namespace forge
