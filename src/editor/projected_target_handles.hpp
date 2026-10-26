#pragma once
#include "animation_debug.hpp"
#include "widgets.hpp"
namespace forge::ui {
// Shared projected axis layout. Callers retain their own space, edit and failure rules.
template <std::size_t N>
std::array<std::optional<ImVec2>, N + 1>
projected_target_handles(const EditorCamera& camera, Double3 center,
                         const std::array<Double3, N>& basis, ImVec2 size) {
    std::array<std::optional<ImVec2>, N + 1> result;
    const auto projected = project_debug_point(camera, center, size);
    if (!projected || size.y <= 0)
        return result;
    result[N] = ImVec2{(*projected)[0], (*projected)[1]};
    const auto eye = camera.eye(), forward = camera.forward();
    double depth = 0;
    for (unsigned i = 0; i < 3; ++i)
        depth += (center[i] - eye[i]) * forward[i];
    const auto scale = camera.pixels_per_unit(depth, size.y);
    const auto length = scale > 0 ? 75 * interface_scale / scale : 0;
    for (unsigned axis = 0; axis < N; ++axis) {
        const auto& direction = basis[axis];
        const auto magnitude = std::hypot(direction[0], direction[1], direction[2]);
        if (!std::isfinite(magnitude) || magnitude < 1e-8)
            continue;
        auto endpoint = center;
        for (unsigned i = 0; i < 3; ++i)
            endpoint[i] += direction[i] * length / magnitude;
        if (const auto point = project_debug_point(camera, endpoint, size)) {
            const auto dx = (*point)[0] - (*projected)[0], dy = (*point)[1] - (*projected)[1];
            if (dx * dx + dy * dy > 144 * interface_scale * interface_scale)
                result[axis] = ImVec2{(*point)[0], (*point)[1]};
        }
    }
    return result;
}
template <std::size_t Count>
int projected_target_hit(const std::array<std::optional<ImVec2>, Count>& points, ImVec2 mouse,
                         float radius, bool center) {
    static_assert(Count > 1);
    if (!points.back())
        return -2;
    const auto start = *points.back();
    if (center && std::abs(mouse.x - start.x) <= radius && std::abs(mouse.y - start.y) <= radius)
        return -1;
    int hit = -2;
    for (unsigned axis = 0; axis + 1 < Count; ++axis) {
        if (!points[axis])
            continue;
        const auto end = *points[axis];
        const auto dx = end.x - start.x, dy = end.y - start.y;
        const auto t = std::clamp(
            ((mouse.x - start.x) * dx + (mouse.y - start.y) * dy) / (dx * dx + dy * dy), .15f, 1.f);
        const auto distance = std::hypot(mouse.x - start.x - dx * t, mouse.y - start.y - dy * t);
        if (distance < radius) {
            hit = int(axis);
            radius = distance;
        }
    }
    return hit;
}
} // namespace forge::ui
