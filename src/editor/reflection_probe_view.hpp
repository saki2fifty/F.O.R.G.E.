#pragma once
#include "../reflection_probe_volume.hpp"
#include "spatial_component_value.hpp"
#include <algorithm>
#include <optional>
namespace forge::ui {
struct ReflectionProbeView {
    ReflectionProbe probe;
    std::array<Double3, 8> outline;
    std::optional<std::array<Double3, 8>> fade_outline;
};
// Detached geometry for guides and framing. Runtime validation and world plane
// distances remain owned by the same probe preparation used for rendering.
inline std::optional<ReflectionProbeView> reflection_probe_view(const Json& row) {
    if (!row.contains("components") || !row.at("components").contains("forge.reflection_probe") ||
        row.value("prefab", false) || !row.value("spatial_resolved", false))
        return {};
    ReflectionProbeView result;
    result.probe =
        spatial_component_value<ReflectionProbe>(row.at("components"), "forge.reflection_probe");
    AffineTransform world;
    world.m = row.at("world_affine").get<std::array<double, 12>>();
    const auto volume = reflection_probe_volume(result.probe, world);
    const std::array extents{result.probe.half_extent_x, result.probe.half_extent_y,
                             result.probe.half_extent_z};
    const auto corners = [&](double inset) {
        std::array<Double3, 8> points;
        for (unsigned index = 0; index < 8; ++index) {
            Double3 local;
            for (unsigned axis = 0; axis < 3; ++axis)
                local[axis] = (index & (1u << axis) ? 1. : -1.) * extents[axis] *
                              (1 - inset / volume.planes[axis][3]);
            points[index] = world.point(local);
        }
        return points;
    };
    result.outline = corners(0);
    if (result.probe.blend_distance > 0 &&
        std::all_of(volume.planes.begin(), volume.planes.end(),
                    [&](const auto& plane) { return plane[3] > result.probe.blend_distance; }))
        result.fade_outline = corners(result.probe.blend_distance);
    return result;
}
} // namespace forge::ui
