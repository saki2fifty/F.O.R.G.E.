#pragma once
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
struct SceneRenderSettings {
    SceneEnvironment environment;
    SceneShadows shadows;
    float exposure = 0; // Authored game exposure in stops; Scene preview has its own override.
    bool operator==(const SceneRenderSettings&) const = default;
};
SceneRenderSettings scene_render_settings(const nlohmann::json& scene);
} // namespace forge
