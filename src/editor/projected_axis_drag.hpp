#pragma once
#include "authoring.hpp"
#include <algorithm>
#include <cmath>
namespace forge {
inline std::optional<Double3> projected_plane_drag(const EditorCamera& camera, Double3 origin,
                                                   Double3 normal, std::array<float, 2> press,
                                                   std::array<float, 2> mouse, float width,
                                                   float height) {
    if (width <= 0 || height <= 0)
        return {};
    const auto dot3 = [](Double3 a, Double3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const auto squared = dot3(normal, normal);
    if (!std::isfinite(squared) || squared < 1e-16 ||
        !std::all_of(origin.begin(), origin.end(), [](double x) { return std::isfinite(x); }))
        return {};
    const auto intersect = [&](std::array<float, 2> pixel) -> std::optional<Double3> {
        const auto ray = view_ray(camera, pixel[0], pixel[1], width, height);
        if (!ray)
            return {};
        const auto& direction = ray->direction;
        const auto& eye = ray->origin;
        const auto denominator = dot3(normal, {direction[0], direction[1], direction[2]});
        if (!std::isfinite(denominator) || std::abs(denominator) < 1e-8)
            return {};
        const auto distance =
            dot3(normal, {origin[0] - eye[0], origin[1] - eye[1], origin[2] - eye[2]}) /
            denominator;
        if (!std::isfinite(distance) || distance < EditorCamera::near_plane ||
            distance > EditorCamera::far_plane)
            return {};
        return Double3{eye[0] + direction[0] * distance, eye[1] + direction[1] * distance,
                       eye[2] + direction[2] * distance};
    };
    const auto first = intersect(press), last = intersect(mouse);
    if (!first || !last)
        return {};
    Double3 delta{};
    for (unsigned i = 0; i < 3; ++i) {
        delta[i] = (*last)[i] - (*first)[i];
        if (!std::isfinite(delta[i]))
            return {};
    }
    return delta;
}
inline std::optional<double> projected_axis_drag(const EditorCamera& camera, Double3 origin,
                                                 Double3 basis, std::array<float, 2> press,
                                                 std::array<float, 2> mouse, float width,
                                                 float height) {
    if (width <= 0 || height <= 0)
        return {};
    const auto dot3 = [](Double3 a, Double3 b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const double squared = dot3(basis, basis);
    if (!std::isfinite(squared) || squared < 1e-16)
        return {};
    Double3 normal{};
    const auto forward = camera.forward();
    const auto parallel = dot3({forward[0], forward[1], forward[2]}, basis) / squared;
    for (unsigned i = 0; i < 3; ++i) {
        normal[i] = forward[i] - basis[i] * parallel;
    }
    if (dot3(normal, normal) < 1e-12)
        return {};
    const auto displacement =
        projected_plane_drag(camera, origin, normal, press, mouse, width, height);
    if (!displacement)
        return {};
    const auto delta = dot3(*displacement, basis) / squared;
    return std::isfinite(delta) ? std::optional<double>(delta) : std::nullopt;
}
} // namespace forge
