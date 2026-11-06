#pragma once
#include <cstdint>
#include <forge/asset_ref.hpp>
namespace forge {
// Imported reflection map and local influence. TRS shapes the volume; map yaw
// remains independent of the volume's rotation and scale.
struct ReflectionProbe {
    AssetRef<TextureAsset> texture;
    bool enabled = true;
    double intensity = 1;
    double half_extent_x = 5, half_extent_y = 5, half_extent_z = 5;
    double blend_distance = 1;
    double rotation_degrees = 0;
    std::int32_t priority = 0;
    bool box_projection = true;
    // Optional local base diffuse illumination from the same convolved map.
    // Zero preserves the Scene environment's diffuse lighting.
    double diffuse_intensity = 0;
    // Derived view-local lighting; the assigned map remains a saved fallback.
    bool runtime_capture = false;
    std::uint32_t capture_size = 128;
    double capture_interval = .25;
    double capture_near = .1, capture_far = 100;
    std::uint32_t capture_layers = UINT32_MAX;
    bool operator==(const ReflectionProbe&) const = default;
};
} // namespace forge
