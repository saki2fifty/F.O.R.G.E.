#pragma once
#include "spatial_component_value.hpp"
#include <algorithm>
#include <array>
#include <forge/render_view.hpp>
#include <optional>
#include <utility>
namespace forge::ui {
// Detached source geometry shared by Scene guides and selection framing.
inline std::array<Double3, 4> rectangle_emitter_outline(const LightView& light, double scale = 1) {
    std::array<Double3, 4> points;
    constexpr std::array<std::array<double, 2>, 4> signs{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned axis = 0; axis < 3; ++axis)
            points[i][axis] =
                light.position[axis] + scale * .5 *
                                           (signs[i][0] * light.emitter_width * light.right[axis] +
                                            signs[i][1] * light.emitter_height * light.up[axis]);
    return points;
}
inline std::optional<std::pair<Double3, Double3>> light_emitter_bounds(const Json& row) {
    if (row.value("prefab", false) || !row.value("spatial_resolved", false) ||
        !row.at("components").contains("forge.light"))
        return {};
    const auto settings = spatial_component_value<Light>(row.at("components"), "forge.light");
    if (settings.kind != unsigned(LightKind::Rectangle) &&
        settings.kind != unsigned(LightKind::Sphere))
        return {};
    AffineTransform world;
    world.m = row.at("world_affine").get<std::array<double, 12>>();
    const auto light = light_view(settings, world);
    std::pair<Double3, Double3> bounds{light.position, light.position};
    if (settings.kind == unsigned(LightKind::Sphere)) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            bounds.first[axis] -= settings.emitter_radius;
            bounds.second[axis] += settings.emitter_radius;
        }
    } else
        for (const auto& point : rectangle_emitter_outline(light))
            for (unsigned axis = 0; axis < 3; ++axis) {
                bounds.first[axis] = std::min(bounds.first[axis], point[axis]);
                bounds.second[axis] = std::max(bounds.second[axis], point[axis]);
            }
    return bounds;
}
} // namespace forge::ui
