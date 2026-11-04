#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace forge {
inline constexpr unsigned fog_volume_profile_version = 1;
inline constexpr unsigned fog_volume_limit = 16;
enum class FogVolumeShape : std::uint32_t { Box, Sphere };
// Homogeneous linear-color geometry fog. Local half extents shape a box or
// ellipsoid; the ordinary entity transform places it in the Scene.
struct FogVolume {
    bool enabled = true;
    std::uint32_t shape = std::uint32_t(FogVolumeShape::Box);
    float color_r = .5f, color_g = .5f, color_b = .5f, density = .15f;
    double half_extent_x = 2, half_extent_y = 2, half_extent_z = 2;
    std::uint32_t layers = UINT32_MAX;
    std::int32_t priority = 0;
    void validate() const {
        if (shape > std::uint32_t(FogVolumeShape::Sphere))
            throw std::invalid_argument("Fog Volume shape must be Box or Sphere");
        for (float channel : {color_r, color_g, color_b})
            if (!std::isfinite(channel) || channel < 0 || channel > 65504)
                throw std::invalid_argument(
                    "Fog Volume color must be finite linear RGB in 0..65504");
        if (!std::isfinite(density) || density < 0 || density > 1000)
            throw std::invalid_argument("Fog Volume density must be finite in 0..1000 per metre");
        for (double extent : {half_extent_x, half_extent_y, half_extent_z})
            if (!std::isfinite(extent) || extent < .001 || extent > 1000000)
                throw std::invalid_argument(
                    "Fog Volume half extents must be in 0.001..1000000 metres");
    }
    bool operator==(const FogVolume&) const = default;
};
} // namespace forge
