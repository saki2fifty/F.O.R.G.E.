#pragma once
#include <forge/fog_volume.hpp>
#include <forge/transform.hpp>
#include <span>
namespace forge {
// Derived camera-relative draw data. The first three rows transform an offset
// from the camera into the volume's canonical unit box/sphere. Remaining rows
// carry linear RGB/density and shape. This stores no authored identity or owner.
struct FogVolumeSample {
    std::array<std::array<float, 4>, 5> rows{};
    bool operator==(const FogVolumeSample&) const = default;
};
inline constexpr double fog_volume_min_world_extent = .000001;
inline constexpr double fog_volume_max_world_extent = 1000000;
inline constexpr double fog_volume_max_view_offset = 1000000;
// Every admitted volume lies within 4e6 metres on each camera-relative axis.
// Ten million covers their complete ray intervals without a large GPU length.
inline constexpr float fog_volume_ray_limit = 10000000;
inline constexpr unsigned fog_volume_uniform_rows = 1 + fog_volume_limit * 5;
using FogVolumeUniform = std::array<std::array<float, 4>, fog_volume_uniform_rows>;
FogVolumeSample prepare_fog_volume(const FogVolume&, const AffineTransform&, Double3 camera_origin);
FogVolumeUniform fog_volume_uniform(std::span<const FogVolumeSample>,
                                    const Double3* orthographic_forward = nullptr);
} // namespace forge
