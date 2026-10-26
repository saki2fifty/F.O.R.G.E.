#pragma once
#include "../json_row_index.hpp"
#include "animation_debug.hpp"
#include "authoring.hpp"
#include "camera_controls.hpp"
#include "editor_state.hpp"
#include "navigation_destination_gesture.hpp"
#include "navigation_region_gesture.hpp"
#include "projected_target_handles.hpp"
#include "rig_target_gesture.hpp"
#include <functional>
namespace forge::ui {
struct SceneTools {
    bool move_tool = true, grid = true, snap = false;
    GridPlane grid_plane = GridPlane::XZ;
    float snap_step = 1, grid_step = 1, fly_speed = 5, exposure = 0;
    MoveGesture move;
    RigTargetGesture rig;
    NavigationRegionGesture region;
    NavigationDestinationGesture destination;
    std::function<std::optional<NavigationDestinationView>(const Json&, const std::string&)>
        destination_view;
    std::string target_component;
    bool destination_focus_requested = false;
    bool active() const {
        return move.active() || rig.active() || region.active() || destination.active();
    }
    void cancel() {
        move.cancel();
        rig.cancel();
        region.cancel();
        destination.cancel();
        destination_focus_requested = false;
    }
    bool valid_for(const Scene& scene) const {
        return move.valid_for(scene) && rig.valid_for(scene) && region.valid_for(scene) &&
               destination.valid_for(scene);
    }
    bool selection_matches(const std::vector<std::string>& ids) const {
        return destination.selection_matches(ids) && region.selection_matches(ids) &&
               move.selection_matches(ids) &&
               (!rig.active() || (ids.size() == 1 && ids.front() == rig.original().entity));
    }
    Json preview(const Json& source) const {
        return destination.active() ? destination.preview(source)
               : region.active()    ? region.preview(source)
               : rig.active()       ? rig.preview(source)
                                    : move.preview(source);
    }
    void target_tool(std::string component) {
        cancel();
        target_component = std::move(component);
        destination_focus_requested = destination_mode();
    }
    ImVec2 press{}, drag_size{}, drag_origin{};
    bool dragged = false;
    Vec3 start_right{}, start_up{};
    EditorCamera drag_camera;
    float units_per_pixel = 1;
    std::array<std::optional<ImVec2>, 4>
    rig_handles(const EditorCamera& camera, const RigTargetView& target, ImVec2 size) const {
        return projected_target_handles(
            camera, target.world(),
            std::array<Double3, 3>{target.axis(0), target.axis(1), target.axis(2)}, size);
    }
    bool destination_mode() const { return target_component == "forge.navigation_agent"; }
    bool focus_destination(EditorCamera& camera, const Json& source, const std::string& selected) {
        if (!destination_focus_requested)
            return false;
        destination_focus_requested = false;
        if (!destination_mode() || !destination_view)
            return false;
        const auto view = destination_view(source, selected);
        if (!view || !view->selectable)
            return false;
        const auto point = navigation_guide_point(view->point, view->plane);
        const Vec3 target{float(point[0]), float(point[1]), float(point[2])};
        if (!std::all_of(target.begin(), target.end(), [](float x) { return std::isfinite(x); }))
            return false;
        camera.target = target; // View navigation only; preserve zoom, projection and orientation.
        return true;
    }
    std::array<std::optional<ImVec2>, 3>
    destination_handles(const EditorCamera& camera, const NavigationDestinationView& target,
                        ImVec2 size) const {
        const Double3 second =
            target.plane == NavigationPlane::PlanarXY ? Double3{0, 1, 0} : Double3{0, 0, 1};
        return projected_target_handles(camera, navigation_guide_point(target.point, target.plane),
                                        std::array<Double3, 2>{Double3{1, 0, 0}, second}, size);
    }
    bool destination_plane_available(const EditorCamera& camera,
                                     const NavigationDestinationView& target, ImVec2 size) const {
        const auto center = navigation_guide_point(target.point, target.plane);
        const auto p = project_debug_point(camera, center, size);
        return p &&
               projected_plane_drag(camera, center,
                                    target.plane == NavigationPlane::PlanarXY ? Double3{0, 0, 1}
                                                                              : Double3{0, 1, 0},
                                    *p, *p, size.x, size.y)
                   .has_value();
    }
    bool destination_input(Scene& scene, const EditorCamera& camera, const std::string& selected,
                           ImVec2 mouse, ImVec2 size, const ViewportInput& input,
                           std::string& status) {
        if (!destination_mode())
            return false;
        const auto current = destination_view
                                 ? destination_view(scene.effective_document(), selected)
                                 : std::optional<NavigationDestinationView>{};
        if (!current || !current->selectable ||
            (destination.active() && !destination.matches(*current))) {
            const bool consumed = destination.active() || input.activated;
            target_tool({});
            status = "Destination tool closed: target, navigation plane or project changed";
            return consumed;
        }
        const auto& io = ImGui::GetIO();
        if (!destination.active() && input.activated && !io.KeyShift && !io.KeyAlt &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const auto handles = destination_handles(camera, *current, size);
            const auto hit =
                projected_target_hit(handles, mouse, 9 * interface_scale,
                                     destination_plane_available(camera, *current, size));
            if (hit != -2 && destination.begin(scene, *current, hit)) {
                move.cancel();
                rig.cancel();
                region.cancel();
                press = mouse;
                drag_size = size;
                drag_camera = camera;
                dragged = false;
            }
        }
        if (!destination.active())
            return false;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
            (!input.active && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
            destination.cancel();
            status = "Destination edit cancelled";
            return true;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragged |= std::hypot(mouse.x - press.x, mouse.y - press.y) > 3;
            if (dragged) {
                const auto& original = destination.original();
                const auto center = navigation_guide_point(original.point, original.plane);
                std::optional<Double3> delta;
                if (destination.axis() < 0)
                    delta = projected_plane_drag(
                        drag_camera, center,
                        original.plane == NavigationPlane::PlanarXY ? Double3{0, 0, 1}
                                                                    : Double3{0, 1, 0},
                        {press.x, press.y}, {mouse.x, mouse.y}, drag_size.x, drag_size.y);
                else {
                    Double3 axis{};
                    axis[destination.axes()[unsigned(destination.axis())]] = 1;
                    if (const auto amount =
                            projected_axis_drag(drag_camera, center, axis, {press.x, press.y},
                                                {mouse.x, mouse.y}, drag_size.x, drag_size.y)) {
                        delta = Double3{};
                        for (unsigned i = 0; i < 3; ++i)
                            (*delta)[i] = axis[i] * *amount;
                    }
                }
                if (!delta || !destination.update(*delta, snap || io.KeyCtrl, snap_step))
                    status = "Destination outside this drag's view or value limits; previous "
                             "preview retained";
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (!dragged)
                destination.cancel();
            else
                try {
                    if (destination.commit(scene))
                        status = "Destination moved. Undo restores the drag.";
                } catch (const std::exception& e) {
                    status = e.what();
                }
        }
        return true;
    }
    bool draw_destination(const Json& doc, const EditorCamera& camera, const std::string& selected,
                          ImVec2 origin, ImVec2 size, bool can_edit = true) const {
        if (!destination_mode() || !can_edit || !destination_view)
            return false;
        const auto target = destination.active()
                                ? std::optional<NavigationDestinationView>(destination.target())
                                : destination_view(doc, selected);
        if (!target || !target->selectable)
            return false;
        const auto points = destination_handles(camera, *target, size);
        if (!points[2])
            return false;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        const ImVec2 center{origin.x + points[2]->x, origin.y + points[2]->y};
        const unsigned second = target->plane == NavigationPlane::PlanarXY ? 1u : 2u;
        const ImU32 colors[] = {IM_COL32(255, 95, 95, 255), IM_COL32(105, 235, 125, 255),
                                IM_COL32(95, 160, 255, 255)};
        for (unsigned i = 0; i < 2; ++i)
            if (points[i]) {
                const ImVec2 end{origin.x + points[i]->x, origin.y + points[i]->y};
                const auto dimension = i ? second : 0;
                draw->AddLine(center, end, colors[dimension], 3 * interface_scale);
                draw->AddCircleFilled(end, 5 * interface_scale, colors[dimension]);
                const char label[] = {"XYZ"[dimension], 0};
                draw->AddText({end.x + 7, end.y - 8}, colors[dimension], label);
                FORGE_UI_RECT_PROBE(
                    "navigation:destination-axis:" + std::to_string(i),
                    (ImVec2{end.x - 6 * interface_scale, end.y - 6 * interface_scale}),
                    (ImVec2{end.x + 6 * interface_scale, end.y + 6 * interface_scale}), true);
            }
        if (destination_plane_available(camera, *target, size)) {
            const float radius = 6 * interface_scale;
            const ImRect box{{center.x - radius, center.y - radius},
                             {center.x + radius, center.y + radius}};
            draw->AddRectFilled(box.Min, box.Max, IM_COL32(255, 203, 94, 255));
            FORGE_UI_RECT_PROBE("navigation:destination-center", box.Min, box.Max, true);
            if (!ImGui::IsAnyMouseDown() && ImGui::IsMouseHoveringRect(box.Min, box.Max))
                show_help_at(
                    "Drag the destination on its navigation plane. Ctrl snaps world metres. "
                    "Release applies one Undo step; Escape cancels. Q/W return to object tools.",
                    box);
        }
        draw->PopClipRect();
        return true;
    }
    bool rig_input(Scene& scene, const EditorCamera& camera, const std::string& selected,
                   ImVec2 mouse, ImVec2 size, const ViewportInput& input, std::string& status) {
        if (destination_mode())
            return false;
        const auto& io = ImGui::GetIO();
        if (!rig.active() && !target_component.empty() && input.activated &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyShift && !io.KeyAlt &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const auto target =
                rig_target_view(scene.effective_document(), selected, target_component);
            if (target) {
                const auto handles = rig_handles(camera, *target, size);
                const auto hit = projected_target_hit(handles, mouse, 9 * interface_scale, false);
                if (hit >= 0 && rig.begin(scene, selected, target_component, unsigned(hit))) {
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
        const auto delta = projected_axis_drag(drag_camera, rig.original().world(),
                                               rig.original().axis(rig.axis()), {press.x, press.y},
                                               {mouse.x, mouse.y}, size.x, size.y);
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
        help("Snap object positions and navigation destinations to the world grid; rig targets "
             "snap in model space. Hold Ctrl "
             "temporarily. Spacing is under View.");
        return changed;
    }
    template <class View>
    bool draw_rig(const Json& document, const View& camera, const std::string& selected,
                  ImVec2 origin, ImVec2 size, bool live = false) const {
        if (target_component.empty() || destination_mode())
            return false;
        const auto target = rig.active() ? std::optional<RigTargetView>(rig.target())
                                         : rig_target_view(document, selected, target_component);
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
    std::array<std::optional<ImVec2>, 4> region_handles(const EditorCamera& camera,
                                                        const NavigationRegionView& view,
                                                        ImVec2 size) const {
        std::array<std::optional<ImVec2>, 4> points;
        const auto center = project_debug_point(camera, view.center(), size);
        if (!center)
            return points;
        for (unsigned dimension = 0; dimension < 2; ++dimension)
            for (unsigned negative = 0; negative < 2; ++negative) {
                auto point = view.center();
                const auto axis = view.axis(dimension);
                const auto extent = dimension == 0 ? view.region.width : view.region.height;
                for (unsigned i = 0; i < 3; ++i)
                    point[i] += .5 * extent * (negative ? -1 : 1) * axis[i];
                const auto screen = project_debug_point(camera, point, size);
                if (screen && std::hypot((*screen)[0] - (*center)[0], (*screen)[1] - (*center)[1]) >
                                  12 * interface_scale)
                    points[dimension * 2 + negative] = ImVec2{(*screen)[0], (*screen)[1]};
            }
        return points;
    }
    bool region_input(Scene& scene, const EditorCamera& camera, const std::string& selected,
                      ImVec2 mouse, ImVec2 size, const ViewportInput& input, std::string& status) {
        const auto& io = ImGui::GetIO();
        if (!region.active() && input.activated && move_tool && target_component.empty() &&
            !io.KeyShift && !io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto view = navigation_region_view(scene.effective_document(), selected);
            if (view) {
                const auto points = region_handles(camera, *view, size);
                for (unsigned i = 0; i < points.size(); ++i)
                    if (points[i] && std::abs(mouse.x - points[i]->x) <= 7 * interface_scale &&
                        std::abs(mouse.y - points[i]->y) <= 7 * interface_scale &&
                        region.begin(scene, selected, i / 2, i % 2 ? -1 : 1)) {
                        press = mouse;
                        drag_size = size;
                        drag_camera = camera;
                        dragged = false;
                        break;
                    }
            }
        }
        if (!region.active())
            return false;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
            ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            region.cancel();
            status = "Region resize cancelled";
            return true;
        }
        dragged |= std::hypot(mouse.x - press.x, mouse.y - press.y) > 2;
        auto anchor = region.original().center();
        const auto basis = region.original().axis(region.dimension());
        const auto extent = region.dimension() == 0 ? region.original().region.width
                                                    : region.original().region.height;
        for (unsigned i = 0; i < 3; ++i)
            anchor[i] += .5 * extent * region.side() * basis[i];
        const auto delta = projected_axis_drag(drag_camera, anchor, basis, {press.x, press.y},
                                               {mouse.x, mouse.y}, size.x, size.y);
        const bool valid =
            delta && (!dragged || region.update(*delta, snap || io.KeyCtrl, snap_step));
        if (!valid)
            status = "Region resize rejected: check the local extent and supported world XY bounds";
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            try {
                if (valid && dragged && region.commit(scene))
                    status = "Navigation region resized. Undo restores the drag.";
                else
                    region.cancel();
            } catch (const std::exception& error) {
                status = error.what();
            }
        }
        return true;
    }
    void input(Scene& scene, const EditorCamera& camera, std::string& selected, ImVec2 origin,
               ImVec2 size, const ViewportInput& input, bool can_edit, std::string& status,
               const std::function<std::string(const Json&, float, float)>& pick = {},
               EditorSelection* selection = nullptr) {
        const auto& io = ImGui::GetIO();
        const ImVec2 mouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
        if (active() &&
            (!can_edit || io.AppFocusLost || !valid_for(scene) ||
             (selection && !selection_matches(selection->entities())) ||
             (!selection && rig.active() && rig.original().entity != selected) ||
             (!selection && region.active() && region.original().entity != selected) ||
             (!selection && destination.active() && destination.original().entity != selected) ||
             ImGui::IsKeyPressed(ImGuiKey_Escape, false) || std::abs(size.x - drag_size.x) > 0.5f ||
             std::abs(size.y - drag_size.y) > 0.5f || std::abs(origin.x - drag_origin.x) > 0.5f ||
             std::abs(origin.y - drag_origin.y) > 0.5f)) {
            cancel();
            status = "Scene gesture cancelled";
            return;
        }
        if (!can_edit || io.AppFocusLost)
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
        if (destination_mode() && selection && selection->entity_count() != 1) {
            target_tool({});
            status = "Destination tool requires one selected agent";
            return;
        }
        const bool was_destination = destination.active();
        if (destination_input(scene, camera, selected, mouse, size, input, status)) {
            if (!was_destination && destination.active())
                drag_origin = origin;
            return;
        }
        if (region.active()) {
            region_input(scene, camera, selected, mouse, size, input, status);
            return;
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
            // Explicit edge squares take priority when a projected Move axis overlaps them.
            if ((!selection || selection->entities().size() == 1) &&
                navigation_region_view(doc, selected) &&
                region_input(scene, camera, selected, mouse, size, input, status)) {
                drag_origin = origin;
                return;
            }
            int hit = -2;
            if (const auto p = entity_position(doc, selected);
                target_component.empty() && move_tool && p && !io.KeyCtrl && !io.KeyShift) {
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
        // Borrow rows for this draw only. A single selection needs no index;
        // larger selections must not rescan the full scene for every guide.
        const auto rows = selection.size() > 1 ? forge::detail::index_json_rows(doc.at("entities"))
                                               : forge::detail::JsonRowIndex{};
        for (const auto& selected_id : selection) {
            const Json* entity = nullptr;
            if (selection.size() > 1) {
                const auto found = rows.find(selected_id);
                if (found != rows.end())
                    entity = found->second;
            } else {
                for (const auto& candidate : doc.at("entities"))
                    if (candidate.at("id") == selected_id) {
                        entity = &candidate;
                        break;
                    }
            }
            if (!entity)
                continue;
            if (const auto center = block_position(*entity)) {
                if (primitive_kind(*entity) != no_primitive) {
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
                if (target_component.empty() && move_tool && can_edit && selected_id == selected) {
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
            if (selected_id == selected && selection.size() == 1 && can_edit && move_tool &&
                target_component.empty()) {
                if (const auto view = navigation_region_view(*entity)) {
                    const auto policies = extract_node_policies(doc);
                    const auto policy = policies.entities.find(entity->at("id").get<EntityId>());
                    if (policy != policies.entities.end() && policy->second.valid &&
                        policy->second.visible && policy->second.selectable) {
                        const auto points = region_handles(camera, *view, size);
                        for (unsigned i = 0; i < points.size(); ++i)
                            if (points[i]) {
                                const ImVec2 point{origin.x + points[i]->x,
                                                   origin.y + points[i]->y};
                                const auto radius = 4 * interface_scale;
                                draw->AddRectFilled({point.x - radius, point.y - radius},
                                                    {point.x + radius, point.y + radius},
                                                    IM_COL32(255, 203, 94, 255));
                                draw->AddRect({point.x - radius, point.y - radius},
                                              {point.x + radius, point.y + radius},
                                              IM_COL32(32, 40, 48, 255));
                                FORGE_UI_PROBE("navigation:region-handle:" + std::to_string(i));
                                if (ImGui::IsMouseHoveringRect(
                                        {point.x - radius, point.y - radius},
                                        {point.x + radius, point.y + radius}) &&
                                    !ImGui::IsAnyMouseDown())
                                    show_help_at(i / 2 == 0 ? "Width: resize around the center. "
                                                              "Ctrl snaps local metres."
                                                            : "Height: resize around the center. "
                                                              "Ctrl snaps local metres.",
                                                 ImRect{{point.x - radius, point.y - radius},
                                                        {point.x + radius, point.y + radius}});
                            }
                    }
                }
            }
        }
        if (!target_component.empty() && can_edit) {
            const auto target = rig.active() ? std::optional<RigTargetView>(rig.target())
                                             : rig_target_view(doc, selected, target_component);
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
                destination.active()
                    ? "Destination | release to apply | Esc cancels | snap in world metres"
                : region.active()
                    ? "Region resize | release to apply | Esc cancels | snap in local metres"
                : rig.active() ? "Rig target | release to apply | Esc cancels | snap in model space"
                : move.active()
                    ? "Moving | release to apply | Esc cancels"
                    : (can_edit
                           ? (destination_mode()
                                  ? "Destination | drag plane axes or center | Q/W: object tools"
                              : !target_component.empty()
                                  ? "Rig target | drag model axes | Q/W: object tools"
                              : move_tool ? "Move | drag axes | Q: select | R: rotate | S: scale"
                                          : "Select | W: show move handles | R: rotate | S: scale")
                           : "Navigation only | object tools inactive"));
        draw->PopClipRect();
    }
};
} // namespace forge::ui
