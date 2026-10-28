#pragma once
#include <array>
#include <forge/asset_ref.hpp>
#include <nlohmann/json.hpp>
namespace forge {
struct TextureAsset;
// Scene-owned authored settings, not an entity transform or a second ECS world.
// Native textures/convolution maps remain derived presentation resources.
struct SceneEnvironment {
    AssetRef<TextureAsset> texture;
    float intensity = 1;
    double rotation = 0; // Radians around world Y; shared by sky and lighting.
    bool sky = true;
    bool operator==(const SceneEnvironment&) const = default;
};
// Current bounded forward shadow profile. Device dimensions and the aggregate
// GPU payload budget are checked separately before native allocation.
inline constexpr unsigned shadow_light_limit = 8;
inline constexpr unsigned shadow_cascade_limit = 8; // Pinned Diligent MAX_CASCADES.
struct SceneShadows {
    bool enabled = true;
    unsigned resolution = 1024, cascades = 4, max_lights = shadow_light_limit;
    double distance = 100; // Metres; directional camera range / unbounded punctual range.
    bool operator==(const SceneShadows&) const = default;
};
struct SceneBloom {
    bool enabled = false;
    float threshold = 1, intensity = .25f, radius = 2;
    bool operator==(const SceneBloom&) const = default;
};
void validate_scene_bloom(const SceneBloom&);
// Scene-wide display adjustments. Filter is linear HDR RGB; saturation and
// contrast operate after tone mapping, before the single sRGB transfer.
struct SceneColorAdjustments {
    bool enabled = false;
    std::array<float, 3> filter{1, 1, 1};
    float saturation = 1, contrast = 1;
    bool operator==(const SceneColorAdjustments&) const = default;
};
void validate_scene_color_adjustments(const SceneColorAdjustments&);
// Automatic gain on the composed Game HDR image. Existing exposure remains an
// additive manual compensation; Scene View retains its independent manual EV.
struct SceneAutoExposure {
    bool enabled = false;
    float min_ev = -8, max_ev = 8;
    float low_percentile = .02f, high_percentile = .98f, target_gray = .18f;
    float speed_up = 1, speed_down = 3; // Exponential rates per unscaled second.
    bool operator==(const SceneAutoExposure&) const = default;
};
void validate_scene_auto_exposure(const SceneAutoExposure&);
struct SceneRenderSettings {
    SceneEnvironment environment;
    SceneShadows shadows;
    SceneBloom bloom;
    float exposure = 0; // Authored game exposure in stops; Scene preview has its own override.
    SceneColorAdjustments color_adjustments;
    SceneAutoExposure auto_exposure;
    bool operator==(const SceneRenderSettings&) const = default;
};
SceneRenderSettings scene_render_settings(const nlohmann::json& scene);
} // namespace forge
