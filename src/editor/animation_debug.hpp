#pragma once
#include "../animation_debug_pose.hpp"
#include "authoring.hpp"
#include <imgui.h>
#include <limits>
namespace forge {
inline std::optional<std::array<float, 2>> debug_screen_point(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > std::numeric_limits<float>::max() ||
        std::abs(y) > std::numeric_limits<float>::max())
        return {};
    return std::array<float, 2>{float(x), float(y)};
}
inline std::optional<std::array<float, 2>> project_debug_point(const GameDebugView& camera,
                                                               Double3 point, ImVec2 size) {
    if (!camera.width || !camera.height)
        return {};
    const auto projected = project_render_point(camera.view, point);
    if (!projected)
        return {};
    return debug_screen_point((*projected)[0] * size.x / camera.width,
                              (*projected)[1] * size.y / camera.height);
}
inline std::optional<std::array<float, 2>> project_debug_point(const EditorCamera& camera,
                                                               Double3 point, ImVec2 size) {
    const auto eye = camera.eye();
    for (unsigned c = 0; c < 3; ++c)
        point[c] -= eye[c];
    auto dot_axis = [&](const auto& axis) {
        return point[0] * axis[0] + point[1] * axis[1] + point[2] * axis[2];
    };
    const auto depth = dot_axis(camera.forward());
    if (size.x <= 0 || size.y <= 0 || depth < EditorCamera::near_plane ||
        depth > EditorCamera::far_plane)
        return {};
    const double scale = EditorCamera::focal * size.y / (2 * depth);
    return debug_screen_point(size.x / 2 + dot_axis(camera.right()) * scale,
                              size.y / 2 - dot_axis(camera.up()) * scale);
}
// Project the same prepared skeleton for each camera; no JSON parsing or source
// node resolution is repeated for additional camera viewports.
template <class View>
inline void draw_animation_debug(std::span<const AnimationDebugSkeleton> skeletons,
                                 const View& camera, ImVec2 origin, ImVec2 size) {
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for (const auto& skeleton : skeletons) {
        const auto& parents = skeleton.parents;
        std::vector<std::optional<std::array<float, 2>>> points;
        for (const auto& point : skeleton.positions)
            points.push_back(point ? project_debug_point(camera, *point, size) : std::nullopt);
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (!points[i])
                continue;
            const ImVec2 p{origin.x + (*points[i])[0], origin.y + (*points[i])[1]};
            const int parent = parents[i];
            if (parent >= 0 && std::size_t(parent) < i && points[parent])
                draw->AddLine(p, {origin.x + (*points[parent])[0], origin.y + (*points[parent])[1]},
                              IM_COL32(100, 225, 190, 255), 2);
            draw->AddCircleFilled(p, 3, IM_COL32(230, 245, 235, 255), 8);
        }
    }
    draw->PopClipRect();
}
} // namespace forge
