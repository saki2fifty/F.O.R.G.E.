#pragma once
#include "authoring.hpp"
#include "camera_controls.hpp"
namespace forge::ui {
struct SceneTools {
    bool move_tool = true, grid = true, snap = false;
    float snap_step = 1, grid_step = 1, fly_speed = 5;
    MoveGesture move;
    ImVec2 press{}, drag_size{};
    bool dragged = false;
    Vec3 start_right{}, start_up{};
    EditorCamera drag_camera;
    float units_per_pixel = 1;
    bool controls() {
        int tool = move_tool ? 1 : 0;
        const int previous = tool;
        ImGui::RadioButton("Select", &tool, 0);
        help("Select objects without move handles. Q while hovering the Scene. R and S still "
             "rotate/scale.");
        ImGui::SameLine();
        ImGui::RadioButton("Move", &tool, 1);
        help("Show X/Y/Z move handles on the selected object. W while hovering the Scene. Drag an "
             "axis or center; Escape cancels.");
        move_tool = tool == 1;
        bool changed = tool != previous;
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Snap", &snap);
        help("Snap moved positions to world-grid multiples. Hold Ctrl temporarily. Spacing is "
             "under View.");
        return changed;
    }
    std::array<std::optional<ImVec2>, 4> handles(const EditorCamera& camera, Vec3 p,
                                                 ImVec2 size) const {
        std::array<std::optional<ImVec2>, 4> result;
        const auto center = project_point(camera, p, size.x, size.y);
        if (!center)
            return result;
        result[3] = ImVec2{(*center)[0], (*center)[1]};
        const float depth = dot(subtract(p, camera.eye()), camera.forward());
        const float length = 75 * interface_scale * 2 * depth / (EditorCamera::focal * size.y);
        for (unsigned i = 0; i < 3; ++i) {
            auto end = p;
            end[i] += length;
            if (auto projected = project_point(camera, end, size.x, size.y)) {
                const auto dx = (*projected)[0] - (*center)[0], dy = (*projected)[1] - (*center)[1];
                if (dx * dx + dy * dy > 144 * interface_scale * interface_scale)
                    result[i] = ImVec2{(*projected)[0], (*projected)[1]};
            }
        }
        return result;
    }
    void input(Scene& scene, const EditorCamera& camera, std::string& selected, ImVec2 origin,
               ImVec2 size, const ViewportInput& input, bool can_edit, std::string& status) {
        const auto& io = ImGui::GetIO();
        const ImVec2 mouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
        if (move.active() &&
            (!can_edit || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
             std::abs(size.x - drag_size.x) > 0.5f || std::abs(size.y - drag_size.y) > 0.5f)) {
            move.cancel();
            status = "Move cancelled";
            return;
        }
        if (!can_edit)
            return;
        if (input.hovered && !io.WantTextInput && !ImGui::IsAnyItemActive() && !io.KeyCtrl &&
            !io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false))
                move_tool = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
                move_tool = false;
        }
        if (input.activated && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const auto doc = render_document(scene.document());
            int hit = -2;
            if (const auto p = entity_position(doc, selected); move_tool && p) {
                const auto points = handles(camera, *p, size);
                if (points[3]) {
                    const auto center = *points[3];
                    const float radius = 9 * interface_scale;
                    if (std::abs(mouse.x - center.x) <= radius &&
                        std::abs(mouse.y - center.y) <= radius)
                        hit = -1;
                    else
                        for (unsigned i = 0; i < 3; ++i) {
                            if (!points[i])
                                continue;
                            const float dx = points[i]->x - center.x, dy = points[i]->y - center.y;
                            const float t =
                                std::clamp(((mouse.x - center.x) * dx + (mouse.y - center.y) * dy) /
                                               (dx * dx + dy * dy),
                                           0.15f, 1.0f);
                            if (std::hypot(mouse.x - center.x - dx * t,
                                           mouse.y - center.y - dy * t) < radius) {
                                hit = int(i);
                                break;
                            }
                        }
                    if (hit != -2 && move.begin(scene, selected, hit)) {
                        press = mouse;
                        drag_size = size;
                        drag_camera = camera;
                        dragged = false;
                        start_right = camera.right();
                        start_up = camera.up();
                        units_per_pixel = 2 * dot(subtract(*p, camera.eye()), camera.forward()) /
                                          (EditorCamera::focal * size.y);
                    }
                }
            }
            if (hit == -2)
                selected = pick_block(doc, camera, mouse.x, mouse.y, size.x, size.y);
        }
        if (move.active()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                move.cancel();
                return;
            }
            const float dx = mouse.x - press.x, dy = mouse.y - press.y;
            Vec3 delta{};
            if (move.axis() < 0) {
                for (unsigned i = 0; i < 3; ++i)
                    delta[i] = (dx * start_right[i] - dy * start_up[i]) * units_per_pixel;
            } else {
                if (const auto distance =
                        axis_drag(drag_camera, move.origin(), move.axis(), {press.x, press.y},
                                  {mouse.x, mouse.y}, size.x, size.y))
                    delta[move.axis()] = *distance;
                else {
                    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                        move.cancel();
                    return;
                }
            }
            dragged |= std::hypot(dx, dy) > 2;
            if (dragged)
                move.update(delta, snap || io.KeyCtrl, snap_step);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                try {
                    if (move.commit(scene))
                        status = "Position moved. Undo restores the entire drag.";
                } catch (const std::exception& e) {
                    status = e.what();
                }
            }
        }
    }
    void draw(const Json& doc, const EditorCamera& camera, const std::string& selected,
              ImVec2 origin, ImVec2 size, bool can_edit) const {
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        auto line = [&](Vec3 a, Vec3 b, ImU32 color, float thickness = 1) {
            float da = dot(subtract(a, camera.eye()), camera.forward());
            float db = dot(subtract(b, camera.eye()), camera.forward());
            const float clip_depth = EditorCamera::near_plane * 1.01f;
            if (da < clip_depth && db < clip_depth)
                return;
            if (da < clip_depth || db < clip_depth) {
                const float t = (clip_depth - da) / (db - da);
                Vec3 clipped;
                for (unsigned i = 0; i < 3; ++i)
                    clipped[i] = a[i] + (b[i] - a[i]) * t;
                if (da < clip_depth)
                    a = clipped;
                else
                    b = clipped;
            }
            const auto p = project_point(camera, a, size.x, size.y),
                       q = project_point(camera, b, size.x, size.y);
            if (p && q)
                draw->AddLine({origin.x + (*p)[0], origin.y + (*p)[1]},
                              {origin.x + (*q)[0], origin.y + (*q)[1]}, color, thickness);
        };
        if (const auto center = entity_position(doc, selected)) {
            const Json* entity = nullptr;
            for (const auto& candidate : doc.at("entities"))
                if (candidate.at("id") == selected)
                    entity = &candidate;
            if (entity) {
                const ObjectTransform transform(*entity);
                for (unsigned corner = 0; corner < 8; ++corner)
                    for (unsigned axis = 0; axis < 3; ++axis)
                        if (!(corner & (1u << axis))) {
                            Vec3 a{}, b;
                            for (unsigned i = 0; i < 3; ++i)
                                a[i] = primitive_kind(*entity) == 3 && i == 1 ? 0
                                       : (corner & (1u << i))                 ? 0.5f
                                                                              : -0.5f;
                            b = a;
                            if (!(primitive_kind(*entity) == 3 && axis == 1))
                                b[axis] += 1;
                            line(transform.point(a), transform.point(b),
                                 IM_COL32(255, 200, 75, 255), 2 * interface_scale);
                        }
            }
            if (move_tool && can_edit) {
                const auto points = handles(camera, *center, size);
                if (points[3]) {
                    const ImVec2 start{origin.x + points[3]->x, origin.y + points[3]->y};
                    const ImU32 colors[] = {IM_COL32(255, 95, 95, 255),
                                            IM_COL32(105, 235, 125, 255),
                                            IM_COL32(95, 160, 255, 255)};
                    const char* labels[] = {"X", "Y", "Z"};
                    for (unsigned i = 0; i < 3; ++i)
                        if (points[i]) {
                            const ImVec2 end{origin.x + points[i]->x, origin.y + points[i]->y};
                            draw->AddLine(start, end, colors[i], 3 * interface_scale);
                            draw->AddCircleFilled(end, 5 * interface_scale, colors[i]);
                            draw->AddText({end.x + 7, end.y - 8}, colors[i], labels[i]);
                        }
                    const float radius = 6 * interface_scale;
                    draw->AddRectFilled({start.x - radius, start.y - radius},
                                        {start.x + radius, start.y + radius},
                                        IM_COL32(245, 225, 130, 255));
                }
            }
        }
        draw->AddText(
            {origin.x + 10, origin.y + size.y - ImGui::GetTextLineHeight() - 10},
            IM_COL32(190, 205, 220, 220),
            move.active()
                ? "Moving | release to apply | Esc cancels"
                : (can_edit ? (move_tool ? "Move | drag axes | Q: select | R: rotate | S: scale"
                                         : "Select | W: show move handles | R: rotate | S: scale")
                            : "Navigation only | object tools inactive"));
        draw->PopClipRect();
    }
};
} // namespace forge::ui
