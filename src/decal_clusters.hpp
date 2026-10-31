#pragma once
#include "decal_shader.hpp"
#include "render_bounds.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace forge {
using DecalTransformRows = std::array<std::array<float, 4>, 3>;
// Transient spatial selection for the existing draw owner. No scene identities,
// material interpretation or resource ownership are stored in this packet.
struct DecalClusters {
    std::array<std::array<float, 4>, 3> controls{}; // camera-relative minimum/maximum/scale
    std::array<std::array<std::uint32_t, 2>, decal_cluster_cells> ranges{};
    std::vector<std::uint32_t> indices;
};
// A missing grid uses the complete ordered storage list. Callers must do the same
// for fragments outside its domain; bounds precision never removes projection.
std::optional<DecalClusters> prepare_decal_clusters(std::span<const DecalTransformRows>,
                                                    const RenderBounds&, Double3 origin);
} // namespace forge
