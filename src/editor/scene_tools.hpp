#pragma once
#include "animation_debug.hpp"
#include "authoring.hpp"
#include "camera_controls.hpp"
#include "editor_state.hpp"
#include "rig_target_gesture.hpp"
#include <functional>
namespace forge::ui {
struct SceneTools {
    bool move_tool = true, grid = true, snap = false;
    GridPlane grid_plane = GridPlane::XZ;
    float snap_step = 1, grid_step = 1, fly_speed = 5, exposure = 0;
    MoveGesture move;
    RigTargetGesture rig;
    std::string rig_component;
    bool active() const { return move.active() || rig.active(); }
    void cancel() {
        move.cancel();
        rig.cancel();
    }
    bool valid_for(const Scene& scene) const {
        return move.valid_for(scene) && rig.valid_for(scene);
    }
    bool selection_matches(const std::vector<std::string>& ids) const {
        return move.selection_matches(ids) &&
               (!rig.active() || (ids.size() == 1 && ids.front() == rig.original().entity));
    }
    Json preview(const Json& source) const {
        return rig.active() ? rig.preview(source) : move.preview(source);
    }
    void target_tool(std::string component) {
        cancel();
        rig_component = std::move(component);
    }
    ImVec2 press{}, drag_size{}, drag_origin{};
    bool dragged = false;
    Vec3 start_right{}, start_up{};
    EditorCamera drag_camera;
    float units_per_pixel = 1;
    std::array<std::optional<ImVec2>, 4>
    rig_handles(const EditorCamera& camera, const RigTargetView& target, ImVec2 size) const {
        std::array<std::optional<ImVec2>, 4> result;
        const auto center = target.world();
        const auto projected = project_debug_point(camera, center, size);
        if (!projected || size.y <= 0)
            return result;
        result[3] = ImVec2{(*projected)[0], (*projected)[1]};
        const auto eye = camera.eye(), forward = camera.forward();
        double depth = 0;
        for (unsigned i = 0; i < 3; ++i)
            depth += (center[i] - eye[i]) * forward[i];
        const auto scale = camera.pixels_per_unit(depth, size.y);
        const auto length = scale > 0 ? 75 * interface_scale / scale : 0;
        for (unsigned axis = 0; axis < 3; ++axis) {
            const auto basis = target.axis(axis);
            const auto magnitude =
                std::sqrt(basis[0] * basis[0] + basis[1] * basis[1] + basis[2] * basis[2]);
            if (!std::isfinite(magnitude) || magnitude < 1e-8)
                continue;
            auto endpoint = center;
            for (unsigned i = 0; i < 3; ++i)
                endpoint[i] += basis[i] * length / magnitude;
            if (const auto point = project_debug_point(camera, endpoint, size)) {
                const auto dx = (*point)[0] - (*projected)[0], dy = (*point)[1] - (*projected)[1];
                if (dx * dx + dy * dy > 144 * interface_scale * interface_scale)
                    result[axis] = ImVec2{(*point)[0], (*point)[1]};
            }
        }
        return result;
    }
    bool rig_input(Scene& scene, const EditorCamera& camera, const std::string& selected,
                   ImVec2 mouse, ImVec2 size, const ViewportInput& input, std::string& status) {
        const auto& io = ImGui::GetIO();
        if (!rig.active() && !rig_component.empty() && input.activated &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyShift && !io.KeyAlt &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const auto target =
                rig_target_view(scene.effective_document(), selected, rig_component);
            if (target) {
                const auto handles = rig_handles(camera, *target, size);
                int hit = -1;
                float distance = 9 * interface_scale;
                if (handles[3])
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        if (!handles[axis])
                            continue;
                        const auto center = *handles[3], end = *handles[axis];
                        const auto dx = end.x - center.x, dy = end.y - center.y;
                        const auto t =
                            std::clamp(((mouse.x - center.x) * dx + (mouse.y - center.y) * dy) /
                                           (dx * dx + dy * dy),
                                       .15f, 1.f);
                        const auto separation =
                            std::hypot(mouse.x - center.x - dx * t, mouse.y - center.y - dy * t);
                        if (separation < distance) {
                            hit = int(axis);
                            distance = separation;
                        }
                    }
                if (hit >= 0 && rig.begin(scene, selected, rig_component, unsigned(hit))) {
                    move.cancel();
                    press = mouse;
                    drag_size = size;
                    drag_camera = camera;
                    dragged = false;
                }
            }
        }
        if (!rig.active())
            return false;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            rig.cancel();
            status = "Rig target cancelled";
            return true;
        }
        const auto dx = mouse.x - press.x, dy = mouse.y - press.y;
        dragged |= std::hypot(dx, dy) > 2;
        const auto delta =
            rig_axis_drag(drag_camera, rig.original().world(), rig.original().axis(rig.axis()),
                          {press.x, press.y}, {mouse.x, mouse.y}, size.x, size.y);
        const bool valid = delta && (!dragged || rig.update(*delta, snap || io.KeyCtrl, snap_step));
        if (delta && !valid)
            status = "Rig target move exceeds its supported range";
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!valid)
                rig.cancel();
            else
                try {
                    if (rig.commit(scene))
                        status = "Rig target moved. Undo restores the entire drag.";
                } catch (const std::exception& e) {
                    status = e.what();
                }
        }
        return true;
    }
    bool controls() {
        int tool = move_tool ? 1 : 0;
        const int previous = tool;
        bool selected_tool = ImGui::RadioButton("Select", &tool, 0);
        help("Select objects without move handles. Q while hovering the Scene. R and S still "
             "rotate/scale.");
        ImGui::SameLine();
        selected_tool |= ImGui::RadioButton("Move", &tool, 1);
        help("Show X/Y/Z move handles on the selected object. W while hovering the Scene. Drag an "
             "axis or center; Escape cancels.");
        move_tool = tool == 1;
        bool changed = tool != previous;
        if (selected_tool)
            target_tool({});
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Snap", &snap);
        help("Snap object positions to the world grid; rig targets snap in model space. Hold Ctrl "
             "temporarily. Spacing is under View.");
        return changed;
    }
    template <class View>
    bool draw_rig(const Json& document, const View& camera, const std::string& selected,
                  ImVec2 origin, ImVec2 size, bool live = false) const {
        if (rig_component.empty())
            return false;
        const auto target = rig.active() ? std::optional<RigTargetView>(rig.target())
                                         : rig_target_view(document, selected, rig_component);
        if (!target)
            return false;
        const auto center = project_debug_point(camera, target->world(), size);
        if (!center || (*center)[0] < 0 || (*center)[0] >= size.x || (*center)[1] < 0 ||
            (*center)[1] >= size.y)
            return false;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        const ImVec2 point{origin.x + (*center)[0], origin.y + (*center)[1]};
        const auto minimum = draw->GetClipRectMin(), maximum = draw->GetClipRectMax();
        if (point.x < minimum.x || point.x >= maximum.x || point.y < minimum.y ||
            point.y >= maximum.y) {
            draw->PopClipRect();
            return false;
        }
        const auto color = target->component == "forge.two_bone_ik" ? IM_COL32(110, 245, 195, 255)
                                                                    : IM_COL32(240, 155, 245, 255);
        draw->AddCircle(point, 8 * interface_scale, color, 16, 2 * interface_scale);
        draw->AddCircleFilled(point, 2 * interface_scale, color);
        const std::string label =
            std::string(live ? "Live " : "") +
            (target->component == "forge.two_bone_ik" ? "Reach target" : "Aim target");
        draw->AddText({point.x + 12 * interface_scale, point.y + 8 * interface_scale}, color,
                      label.c_str());
        draw->PopClipRect();
        return true;
    }
    std::array<std::optional<ImVec2>, 4> handles(const EditorCamera& camera, Vec3 p,
                                                 ImVec2 size) const {
        std::array<std::optional<ImVec2>, 4> result;
        const auto center = project_point(camera, p, size.x, size.y);
        if (!center)
            return result;
        result[3] = ImVec2{(*center)[0], (*center)[1]};
        const float depth = dot(subtract(p, camera.eye()), camera.forward());
        const float length = 75 * interface_scale * camera.world_units_per_pixel(depth, size.y);
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
               ImVec2 size, const ViewportInput& input, bool can_edit, std::string& status,
               const std::function<std::string(const Json&, float, float)>& pick = {},
               EditorSelection* selection = nullptr) {
        const auto& io = ImGui::GetIO();
        const ImVec2 mouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
        if (active() &&
            (!can_edit || !valid_for(scene) ||
             (selection && !selection_matches(selection->entities())) ||
             (!selection && rig.active() && rig.original().entity != selected) ||
             ImGui::IsKeyPressed(ImGuiKey_Escape, false) || std::abs(size.x - drag_size.x) > 0.5f ||
             std::abs(size.y - drag_size.y) > 0.5f || std::abs(origin.x - drag_origin.x) > 0.5f ||
             std::abs(origin.y - drag_origin.y) > 0.5f)) {
            cancel();
            status = "Scene gesture cancelled";
            return;
        }
        if (!can_edit)
            return;
        if (input.hovered && !io.WantTextInput && !ImGui::IsAnyItemActive() && !io.KeyCtrl &&
            !io.KeyAlt && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                move_tool = true;
                target_tool({});
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
                move_tool = false;
                target_tool({});
            }
        }
        const bool was_rig = rig.active();
        if (rig_input(scene, camera, selected, mouse, size, input, status)) {
            if (!was_rig && rig.active())
                drag_origin = origin;
            return;
        }
        if (input.activated && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const auto doc = scene.effective_document();
            int hit = -2;
            if (const auto p = entity_position(doc, selected);
                rig_component.empty() && move_tool && p && !io.KeyCtrl && !io.KeyShift) {
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
                    if (hit != -2 && move.begin(scene, selected, hit,
                                                selection ? selection->entities()
                                                          : std::vector<std::string>{})) {
                        press = mouse;
                        drag_size = size;
                        drag_origin = origin;
                        drag_camera = camera;
                        dragged = false;
                        start_right = camera.right();
                        start_up = camera.up();
                        units_per_pixel = camera.world_units_per_pixel(
                            dot(subtract(*p, camera.eye()), camera.forward()), size.y);
                    }
                }
            }
            if (hit == -2) {
                try {
                    const auto hit_entity =
                        pick ? pick(doc, mouse.x, mouse.y)
                             : pick_block(doc, camera, mouse.x, mouse.y, size.x, size.y);
                    if (selection)
                        selection->click_entity(hit_entity, io.KeyCtrl || io.KeyShift);
                    else
                        selected = hit_entity;
                } catch (const std::exception& e) {
                    status = std::string("Selection unchanged: ") + e.what();
                }
            }
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
              ImVec2 origin, ImVec2 size, bool can_edit,
              std::vector<std::string> selection = {}) const {
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
        if (selection.empty() && !selected.empty())
            selection.push_back(selected);
        for (const auto& selected_id : selection) {
            if (const auto center = entity_position(doc, selected_id)) {
                const Json* entity = nullptr;
                for (const auto& candidate : doc.at("entities"))
                    if (candidate.at("id") == selected_id)
                        entity = &candidate;
                if (entity && primitive_kind(*entity) != no_primitive) {
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
                if (const auto point = project_point(camera, *center, size.x, size.y)) {
                    draw->AddCircle({origin.x + (*point)[0], origin.y + (*point)[1]},
                                    5 * interface_scale, IM_COL32(255, 200, 75, 255), 12,
                                    interface_scale);
                }
                if (rig_component.empty() && move_tool && can_edit && selected_id == selected) {
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
        }
        if (!rig_component.empty() && can_edit) {
            const auto target = rig.active() ? std::optional<RigTargetView>(rig.target())
                                             : rig_target_view(doc, selected, rig_component);
            if (target) {
                const auto points = rig_handles(camera, *target, size);
                if (points[3]) {
                    const ImVec2 start{origin.x + points[3]->x, origin.y + points[3]->y};
                    const ImU32 colors[] = {IM_COL32(255, 95, 95, 255),
                                            IM_COL32(105, 235, 125, 255),
                                            IM_COL32(95, 160, 255, 255)};
                    for (unsigned i = 0; i < 3; ++i)
                        if (points[i]) {
                            const ImVec2 end{origin.x + points[i]->x, origin.y + points[i]->y};
                            draw->AddLine(start, end, colors[i], 3 * interface_scale);
                            draw->AddCircleFilled(end, 5 * interface_scale, colors[i]);
                            const char label[] = {"XYZ"[i], 0};
                            draw->AddText({end.x + 7, end.y - 8}, colors[i], label);
                        }
                }
            }
        }
        // Preserve image space when docked panels or interface zoom leave a
        // shallow viewport. The same controls remain available in its tooltip.
        if (size.y >= 3 * ImGui::GetTextLineHeight() + 20)
            draw->AddText(
                {origin.x + 10, origin.y + size.y - ImGui::GetTextLineHeight() - 10},
                IM_COL32(190, 205, 220, 220),
                rig.active() ? "Rig target | release to apply | Esc cancels | snap in model space"
                : move.active()
                    ? "Moving | release to apply | Esc cancels"
                    : (can_edit
                           ? (!rig_component.empty()
                                  ? "Rig target | drag model axes | Q/W: object tools"
                              : move_tool ? "Move | drag axes | Q: select | R: rotate | S: scale"
                                          : "Select | W: show move handles | R: rotate | S: scale")
                           : "Navigation only | object tools inactive"));
        draw->PopClipRect();
    }
};
} // namespace forge::ui
