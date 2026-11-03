#pragma once
#include <array>
#include <forge/terrain_source.hpp>
#include <optional>
#include <span>
#include <vector>
namespace forge::detail {
// Transient queries against one validated float-triangle surface. No file access,
// persistent cache or second terrain representation is owned by the caller.
std::vector<std::optional<double>>
terrain_heights(const TerrainRaster&, std::span<const std::array<double, 2>> positions);
} // namespace forge::detail
