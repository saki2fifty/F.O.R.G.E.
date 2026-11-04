#pragma once
#include <array>
#include <forge/terrain_source.hpp>
#include <optional>
#include <span>
#include <vector>
namespace forge::detail {
struct TerrainSurfaceSample {
    double height = 0;
    // Unnormalized upward geometric triangle normal, not the smooth shading normal.
    std::array<double, 3> normal{};
};
std::vector<std::optional<TerrainSurfaceSample>>
terrain_surfaces(const TerrainRaster&, std::span<const std::array<double, 2>> positions);
// Transient queries against one validated float-triangle surface. No file access,
// persistent cache or second terrain representation is owned by the caller.
std::vector<std::optional<double>>
terrain_heights(const TerrainRaster&, std::span<const std::array<double, 2>> positions);
} // namespace forge::detail
