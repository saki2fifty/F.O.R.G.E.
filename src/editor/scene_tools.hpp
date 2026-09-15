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
        bool changed = ImGui::Checkbox("Move handles", &move_tool);
        help("Show world-axis translation handles. Drag an axis or the center square. Escape "
             "cancels.");
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Grid", &grid);
        help("Show a world XZ reference grid overlay at Y=0. It is not depth-tested against "
             "blocks.");
        changed |= ImGui::Checkbox("Snap", &snap);
        help("Snap moved coordinates to world-grid multiples. Hold Ctrl during a drag to "
             "temporarily snap.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90 * interface_scale);
        changed |= ImGui::DragFloat("Step", &snap_step, 0.05f, 0.01f, 1000, "%.2f",
                                    ImGuiSliderFlags_AlwaysClamp);
        help("Move snap spacing in world units, from 0.01 to 1000. Also used by Snap position.");
        if (ImGui::TreeNode("View settings")) {
            help(
                "Persistent grid spacing and flight speed. Camera bookmarks belong to each scene.");
            changed |= ImGui::DragFloat("Grid spacing", &grid_step, 0.1f, 0.1f, 1000, "%.1f",
                                        ImGuiSliderFlags_AlwaysClamp);
            help("Spacing of the editor-only reference grid, in world units.");
            changed |= ImGui::DragFloat("Fly speed", &fly_speed, 0.2f, 0.1f, 1000, "%.1f",
                                        ImGuiSliderFlags_AlwaysClamp);
            help("RMB+WASD/Space/Shift flight speed in world units per second.");
            ImGui::TreePop();
        }
        help("Expand grid spacing and flight speed controls.");
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
        if (input.activated && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const auto doc = scene.document();
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
        if (grid) {
            const float cx = std::round(camera.target[0] / grid_step) * grid_step;
            const float cz = std::round(camera.target[2] / grid_step) * grid_step;
            const float range = 20 * grid_step;
            for (int i = -20; i <= 20; ++i) {
                const float x = cx + i * grid_step, z = cz + i * grid_step;
                line({x, 0, cz - range}, {x, 0, cz + range}, IM_COL32(110, 135, 160, 75));
                line({cx - range, 0, z}, {cx + range, 0, z}, IM_COL32(110, 135, 160, 75));
            }
            line({cx - range, 0, 0}, {cx + range, 0, 0}, IM_COL32(240, 90, 90, 150));
            line({0, 0, cz - range}, {0, 0, cz + range}, IM_COL32(90, 150, 255, 150));
        }
        if (const auto center = entity_position(doc, selected)) {
            for (unsigned corner = 0; corner < 8; ++corner)
                for (unsigned axis = 0; axis < 3; ++axis)
                    if (!(corner & (1u << axis))) {
                        Vec3 a = *center, b;
                        for (unsigned i = 0; i < 3; ++i)
                            a[i] += (corner & (1u << i)) ? 0.51f : -0.51f;
                        b = a;
                        b[axis] += 1.02f;
                        line(a, b, IM_COL32(255, 200, 75, 255), 2 * interface_scale);
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
        draw->AddText({origin.x + 10, origin.y + size.y - ImGui::GetTextLineHeight() - 10},
                      IM_COL32(190, 205, 220, 220),
                      move.active()
                          ? "Moving | release to apply | Esc cancels"
                          : (can_edit ? "LMB select | axis / center drag moves | Ctrl snaps"
                                      : "Navigation only | move tools inactive"));
        draw->PopClipRect();
    }
};
} // namespace forge::ui
