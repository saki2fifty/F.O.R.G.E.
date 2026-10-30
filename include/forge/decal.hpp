#pragma once
#include <forge/asset_ref.hpp>
namespace forge {
inline constexpr float decal_min_dimension = .001f, decal_max_dimension = 10000;
inline constexpr float decal_max_angle = 1.5707963267948966f;
// Projected material values. Node transforms, resource leases and GPU packets
// remain with their existing owners; none is stored on this component.
struct DecalProjector {
    AssetRef<MaterialAsset> material;
    bool enabled = true;
    float width = 1, height = 1, depth = 1; // Full local metres before node scale.
    float tint_r = 1, tint_g = 1, tint_b = 1, opacity = 1;
    float angle_fade_start = .7853981633974483f, angle_fade_end = 1.48352986419518f;
    std::uint32_t layers = UINT32_MAX;
    std::int32_t priority = 0; // Ascending composition; higher priority paints last.
    float color_influence = 1, normal_influence = 0, roughness_influence = 0;
    bool operator==(const DecalProjector&) const = default;
};
} // namespace forge
