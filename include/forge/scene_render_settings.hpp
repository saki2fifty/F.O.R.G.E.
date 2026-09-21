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
struct SceneRenderSettings {
    SceneEnvironment environment;
    float exposure = 0; // Authored game exposure in stops; Scene preview has its own override.
    bool operator==(const SceneRenderSettings&) const = default;
};
SceneRenderSettings scene_render_settings(const nlohmann::json& scene);
} // namespace forge
