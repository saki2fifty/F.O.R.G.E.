#pragma once
#include "editor/component_inspector.hpp"
#include "editor/scene_tools.hpp"
inline void check_destination_input(forge::NavigationTools& navigation, forge::Scene& scene,
                                    forge::SceneDocument& project, const std::string& actor,
                                    const std::string& other, forge::EditorCamera camera,
                                    ImVec2 origin, ImVec2 size) {
    using namespace forge;
    auto& io = ImGui::GetIO();
    camera.target = {100, 100, 100}; // Entry must reveal a destination outside the old view.
    const auto distance = camera.distance;
    io.ConfigInputTrickleEventQueue = false;
    ui::SceneTools tools;
    tools.destination_view =
        [&](const Json& source,
            const std::string& entity) -> std::optional<NavigationDestinationView> {
        if (!navigation.overlay_visible())
            return {};
        return navigation.requested_destination(source, entity);
    };
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.navigation_agent";
    inspector.scene_target_available = [&](const std::string& entity, const std::string&) {
        const auto view = navigation.requested_destination(scene.effective_document(), entity);
        return view && view->selectable;
    };
    std::string selected = other, status;
    unsigned activations = 0, picks = 0;
    inspector.edit_scene_target = [&](const std::string& entity, const std::string& component) {
        check(entity == actor && component == "forge.navigation_agent",
              "Pinned Inspector targeted another entity");
        ++activations;
        selected = entity;
        navigation.show_overlay(true);
        tools.target_tool(component);
    };
    bool inspect = true, can_edit = true;
    ImVec2 area = size;
    const auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin(inspect ? "Pinned destination Inspector" : "Destination Scene");
        if (inspect)
            inspector.draw(scene, project, actor);
        else {
            tools.focus_destination(camera, scene.effective_document(), selected);
            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton("Destination viewport", area, ImGuiButtonFlags_MouseButtonLeft);
            const ui::ViewportInput input{ImGui::IsItemHovered(), ImGui::IsItemActive(),
                                          ImGui::IsItemActivated()};
            tools.input(scene, camera, selected, origin, area, input, can_edit, status,
                        [&](const Json&, float, float) {
                            ++picks;
                            return other;
                        });
            const auto shown = tools.active()
                                   ? scene.preview_document(tools.preview(scene.document()))
                                   : scene.effective_document();
            tools.draw(shown, camera, selected, origin, area, can_edit);
            navigation.draw(shown, camera, origin, area, selected);
            tools.draw_destination(shown, camera, selected, origin, area, can_edit);
        }
        ImGui::End();
        ImGui::Render();
    };
    navigation.show_overlay(false);
    frame();
    frame();
    const auto eligibility_source = scene.document();
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.navigation_agent"},
                       {"field", "has_destination"},
                       {"value", false}});
    frame();
    check(!test::ui_targets.at("navigation:destination-tool").enabled && scene.undo() &&
              scene.document() == eligibility_source,
          "Cleared destination acquired an Inspector target tool");
    const bool had_policy = scene.entity(actor).has<NodeSelectability>();
    if (!had_policy)
        authoring_command(scene, "component.add",
                          {{"entity", actor}, {"component", "forge.node_selectability"}});
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.node_selectability"},
                       {"field", "selectable"},
                       {"value", false}});
    frame();
    check(!test::ui_targets.at("navigation:destination-tool").enabled && scene.undo(),
          "Selection-locked destination acquired an Inspector target tool");
    if (!had_policy)
        check(scene.undo(), "Could not restore optional selection policy fixture");
    check(scene.document() == eligibility_source, "Eligibility check changed source intent");
    frame();
    auto button = test::ui_targets.at("navigation:destination-tool");
    // Restoring the optional policy changes content height. Let the real window
    // apply its pending scroll/clamp before aiming a click at rendered geometry.
    for (unsigned attempt = 0;; ++attempt) {
        check(attempt < 20, "Destination Inspector action did not settle into view");
        const auto previous = button;
        if (button.maximum.y > button.clip_maximum.y || button.minimum.y < button.clip_minimum.y) {
            io.AddMousePosEvent((button.clip_minimum.x + button.clip_maximum.x) * .5f,
                                (button.clip_minimum.y + button.clip_maximum.y) * .5f);
            io.AddMouseWheelEvent(0, button.minimum.y < button.clip_minimum.y ? 2 : -2);
        }
        frame();
        button = test::ui_targets.at("navigation:destination-tool");
        if (button.minimum.y == previous.minimum.y && button.maximum.y == previous.maximum.y &&
            button.minimum.y >= button.clip_minimum.y && button.maximum.y <= button.clip_maximum.y)
            break;
    }
    check(button.enabled, "Saved-asset destination Inspector action is disabled");
    io.AddMousePosEvent((button.minimum.x + button.maximum.x) * .5f,
                        (button.minimum.y + button.maximum.y) * .5f);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    if (activations != 1 || selected != actor || !tools.destination_mode() ||
        !navigation.overlay_visible()) {
        const auto actual = test::ui_targets.at("navigation:destination-tool");
        throw std::runtime_error(
            "Inspector destination entry: activations=" + std::to_string(activations) + ", scale=" +
            std::to_string(ui::interface_scale) + ", display=" + std::to_string(io.DisplaySize.x) +
            ", expected_y=" + std::to_string(button.minimum.y) + ", actual_y=" +
            std::to_string(actual.minimum.y) + ", mouse_y=" + std::to_string(io.MousePos.y));
    }
    inspect = false;
    frame();
    frame();
    const auto focused = navigation.requested_destination(scene.effective_document(), actor);
    check(focused.has_value(), "Destination focus lost the current saved asset view");
    const auto center = navigation_guide_point(focused->point, focused->plane);
    check(camera.target ==
                  EditorCamera::Vec{float(center[0]), float(center[1]), float(center[2])} &&
              camera.distance == distance,
          "Destination entry did not reveal the requested point while preserving zoom");
    const auto baseline = scene.document();
    const auto original = scene.entity(actor).get<NavigationAgent>();
    const auto press = [&](int axis) {
        const auto target = navigation.requested_destination(scene.effective_document(), actor);
        check(bool(target), "Destination input lost saved navigation metadata");
        const auto points = tools.destination_handles(camera, *target, area);
        check(points[2].has_value() && (axis < 0 || points[unsigned(axis)].has_value()),
              "Destination input handle is not projectable");
        const auto center = *points[2];
        const auto point = axis < 0
                               ? center
                               : ImVec2{center.x + (points[unsigned(axis)]->x - center.x) * .45f,
                                        center.y + (points[unsigned(axis)]->y - center.y) * .45f};
        const ImVec2 pixel{origin.x + point.x, origin.y + point.y};
        io.AddMousePosEvent(pixel.x, pixel.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        check(tools.destination.active() && !tools.move.active() && !tools.rig.active() &&
                  !tools.region.active(),
              "Destination handle failed exclusive input ownership");
        return pixel;
    };
    const auto release = [&] {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    };
    auto point = press(0);
    io.AddMousePosEvent(point.x + 45, point.y);
    frame();
    check(scene.document() == baseline && tools.destination.active(),
          "Actual destination drag wrote source during preview");
    release();
    const auto moved = scene.document();
    const auto changed = scene.entity(actor).get<NavigationAgent>();
    check(moved != baseline && changed.destination_x != original.destination_x &&
              changed.destination_y == original.destination_y &&
              changed.destination_z == original.destination_z && scene.undo() &&
              scene.document() == baseline && scene.redo() && scene.document() == moved &&
              scene.undo(),
          "Actual axis destination release did not preserve coordinates and one Undo/Redo step");
    const auto revision = scene.revision();
    press(-1);
    release();
    check(scene.revision() == revision && scene.document() == baseline,
          "Destination click without drag created history");
    point = press(-1);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddMousePosEvent(point.x + 45, point.y - 25);
    frame();
    release();
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame();
    const auto snapped = scene.entity(actor).get<NavigationAgent>();
    const auto plane = navigation.requested_destination(scene.effective_document(), actor)->plane;
    check(snapped.destination_x == std::round(snapped.destination_x) &&
              (plane == NavigationPlane::PlanarXY
                   ? snapped.destination_y == std::round(snapped.destination_y) &&
                         snapped.destination_z == original.destination_z
                   : snapped.destination_z == std::round(snapped.destination_z) &&
                         snapped.destination_y == original.destination_y) &&
              scene.document() != baseline && scene.undo() && scene.document() == baseline,
          "Actual plane destination drag did not snap world coordinates or preserve the normal "
          "coordinate");
    for (unsigned cancel = 0; cancel < 8; ++cancel) {
        point = press(0);
        io.AddMousePosEvent(point.x + 30, point.y);
        frame();
        if (cancel == 0)
            io.AddKeyEvent(ImGuiKey_Escape, true);
        if (cancel == 1)
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
        if (cancel == 2)
            io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
        if (cancel == 3)
            can_edit = false;
        if (cancel == 4)
            area.x -= 20;
        if (cancel == 5)
            selected = other;
        if (cancel == 6)
            navigation.show_overlay(false);
        if (cancel == 7)
            io.AddFocusEvent(false);
        frame();
        if (tools.destination.active() || scene.document() != baseline)
            throw std::runtime_error(
                "Destination cancel case " + std::to_string(cancel) +
                ": active=" + std::to_string(tools.destination.active()) + ", source_changed=" +
                std::to_string(scene.document() != baseline) + ", status=" + status);
        io.AddKeyEvent(ImGuiKey_Escape, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
        if (cancel == 7)
            io.AddFocusEvent(true);
        release();
        can_edit = true;
        area = size;
        selected = actor;
        navigation.show_overlay(true);
        tools.target_tool("forge.navigation_agent");
        frame();
    }
    io.AddMousePosEvent(origin.x + area.x * .5f, origin.y + area.y * .5f);
    frame();
    io.AddKeyEvent(ImGuiKey_Q, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Q, false);
    frame();
    check(tools.target_component.empty() && !tools.move_tool,
          "Q did not restore ordinary selection mode");
    tools.target_tool("forge.navigation_agent");
    io.AddKeyEvent(ImGuiKey_W, true);
    frame();
    io.AddKeyEvent(ImGuiKey_W, false);
    frame();
    check(tools.target_component.empty() && tools.move_tool && scene.document() == baseline &&
              !picks,
          "W did not restore object Move or a target handle leaked into selection");
}
