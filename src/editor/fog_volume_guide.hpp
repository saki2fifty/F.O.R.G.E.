#pragma once
#include "../fog_volume_geometry.hpp"
#include "spatial_component_value.hpp"
#include <optional>
namespace forge::ui {
struct FogVolumeGuide {
    FogVolume values;
    Double3 center;
    std::array<Double3, 3> axes;
    std::array<Double3, 8> outline;
};
// Detached guides use the renderer's geometry admission; no helper entities.
inline std::optional<FogVolumeGuide> fog_volume_guide(const Json& row) {
    if (!row.contains("components") || !row.at("components").contains("forge.fog_volume") ||
        row.value("prefab", false) || !row.value("spatial_resolved", false))
        return {};
    FogVolumeGuide result;
    result.values = spatial_component_value<FogVolume>(row.at("components"), "forge.fog_volume");
    AffineTransform world;
    world.m = row.at("world_affine").get<std::array<double, 12>>();
    result.center = world.point({0, 0, 0});
    (void)prepare_fog_volume(result.values, world, result.center);
    const std::array extents{result.values.half_extent_x, result.values.half_extent_y,
                             result.values.half_extent_z};
    for (unsigned axis = 0; axis < 3; ++axis)
        for (unsigned row = 0; row < 3; ++row)
            result.axes[axis][row] = world.m[row * 4 + axis] * extents[axis];
    for (unsigned index = 0; index < 8; ++index) {
        Double3 local;
        for (unsigned axis = 0; axis < 3; ++axis)
            local[axis] = (index & (1u << axis) ? 1. : -1.) * extents[axis];
        result.outline[index] = world.point(local);
    }
    return result;
}
} // namespace forge::ui
