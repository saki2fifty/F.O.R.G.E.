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
// Radial geometry fog in linear HDR, before blending and display processing.
// Sky/background, editor overlays and HUD retain their own colors.
struct SceneDistanceFog {
    bool enabled = false;
    std::array<float, 3> color{.5f, .5f, .5f};
    float start = 0, end = 100; // World metres from each camera's position.
    bool operator==(const SceneDistanceFog&) const = default;
};
void validate_scene_distance_fog(const SceneDistanceFog&);
// History weight for depth-writing Mesh geometry. Other layers remain current-frame draws.
struct SceneTemporalAntiAliasing {
    bool enabled = false;
    float stability = .8f;
    bool operator==(const SceneTemporalAntiAliasing&) const = default;
};
void validate_scene_temporal_anti_aliasing(const SceneTemporalAntiAliasing&);
struct SceneDepthOfField {
    bool enabled = false;
    float focus_distance = 10;
    float focus_range = 5;
    float blur_radius = 8;
    bool operator==(const SceneDepthOfField&) const = default;
};
void validate_scene_depth_of_field(const SceneDepthOfField&);
struct SceneMotionBlur {
    bool enabled = false;
    float shutter = .5f;
    float max_pixels = 32;
    bool operator==(const SceneMotionBlur&) const = default;
};
void validate_scene_motion_blur(const SceneMotionBlur&);
struct SceneAmbientOcclusion {
    bool enabled = false;
    float radius = .5f;
    float strength = 1;
    float bias = .01f;
    float normal_influence = 0;
    bool operator==(const SceneAmbientOcclusion&) const = default;
};
void validate_scene_ambient_occlusion(const SceneAmbientOcclusion&);
struct SceneScreenSpaceReflections {
    bool enabled = false;
    float max_distance = 25;
    float thickness = .1f;
    float strength = 1;
    float max_roughness = .5f;
    bool roughness_filtering = false;
    bool operator==(const SceneScreenSpaceReflections&) const = default;
};
void validate_scene_screen_space_reflections(const SceneScreenSpaceReflections&);
struct SceneRenderSettings {
    SceneEnvironment environment;
    SceneShadows shadows;
    SceneBloom bloom;
    float exposure = 0; // Authored game exposure in stops; Scene preview has its own override.
    SceneColorAdjustments color_adjustments;
    SceneAutoExposure auto_exposure;
    SceneDistanceFog distance_fog;
    SceneTemporalAntiAliasing temporal_anti_aliasing;
    SceneDepthOfField depth_of_field;
    SceneMotionBlur motion_blur;
    SceneAmbientOcclusion ambient_occlusion;
    SceneScreenSpaceReflections screen_space_reflections;
    bool operator==(const SceneRenderSettings&) const = default;
};
SceneRenderSettings scene_render_settings(const nlohmann::json& scene);
} // namespace forge
