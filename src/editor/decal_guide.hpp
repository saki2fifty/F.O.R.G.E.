#pragma once
#include "spatial_component_value.hpp"
#include <cmath>
#include <forge/render_view.hpp>
#include <optional>
namespace forge::ui {
struct DecalGuide {
    DecalView projector;
    std::array<Double3, 8> outline;
};
// Detached editor geometry from the same admitted local dimensions and affine
// transform as rendering. No hidden helper entities or authored guide state.
inline std::optional<DecalGuide> decal_guide(const Json& row) {
    if (!row.contains("components") || !row.at("components").contains("forge.decal") ||
        row.value("prefab", false) || !row.value("spatial_resolved", false))
        return {};
    AffineTransform world;
    world.m = row.at("world_affine").get<std::array<double, 12>>();
    DecalGuide result;
    result.projector = forge::decal_view(
        spatial_component_value<DecalProjector>(row.at("components"), "forge.decal"), world);
    const auto& value = result.projector.values;
    const std::array dimensions{value.width, value.height, value.depth};
    for (unsigned i = 0; i < result.outline.size(); ++i) {
        Double3 local;
        for (unsigned axis = 0; axis < 3; ++axis)
            local[axis] = (i & (1u << axis) ? .5 : -.5) * dimensions[axis];
        result.outline[i] = world.point(local);
        for (const auto coordinate : result.outline[i])
            if (!std::isfinite(coordinate))
                throw std::runtime_error("Decal guide exceeds finite world coordinates");
    }
    return result;
}
} // namespace forge::ui
