#pragma once
#include "authoring.hpp"
#include <imgui.h>
namespace forge {
// Same camera projection and overlay convention as the existing transform gizmos.
template <class View>
inline void draw_animation_debug(const Json& document, const View& camera, ImVec2 origin,
                                 ImVec2 size) {
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    for (const auto& item : document.at("entities")) {
        if (!item.contains("animation_pose") || !item.value("spatial_resolved", true))
            continue;
        const ObjectTransform owner(item);
        const auto& pose = item.at("animation_pose");
        const auto& matrices = pose.at("model");
        const auto& parents = pose.at("parents");
        if (matrices.size() != parents.size() || matrices.size() > 1024)
            continue;
        std::vector<std::optional<std::array<float, 2>>> points;
        for (const auto& matrix : matrices) {
            if (matrix.size() != 16) {
                points.push_back({});
                continue;
            }
            const auto world = owner.point({matrix.at(12), matrix.at(13), matrix.at(14)});
            points.push_back(project_point(camera, world, size.x, size.y));
        }
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
