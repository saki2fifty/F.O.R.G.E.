#pragma once
#include "editor/scene_tools.hpp"
inline void check_region_resize_input(forge::Scene& scene, const std::string& id, float scale,
                                      bool narrow = false) {
    using namespace forge;
    ui::SceneTools tools;
    EditorCamera camera;
    camera.align(2, -1);
    camera.target = {0, 0, 0};
    camera.projection = CameraProjection::Orthographic;
    camera.orthographic_height = 20;
    camera.distance = 20;
    ImVec2 origin{10, 40}, size = narrow ? ImVec2{420, 640} : ImVec2{1000, 800};
    const float metre_pixels = size.y / camera.orthographic_height;
    auto selected = id;
    std::string status;
    bool can_edit = true;
    unsigned picks = 0;
    ui::SpatialHelpers helpers;
    std::uint64_t helper_generation = 0;
    auto& io = ImGui::GetIO();
    const auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1100, 900}, ImGuiCond_Always);
        ImGui::Begin("Navigation region resize input");
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("Scene image", size, ImGuiButtonFlags_MouseButtonLeft);
        const ui::ViewportInput input{ImGui::IsItemHovered(), ImGui::IsItemActive(),
                                      ImGui::IsItemActivated()};
        helpers.update(scene.effective_document(), ++helper_generation, unsigned(size.x),
                       unsigned(size.y));
        tools.input(scene, camera, selected, origin, size, input, can_edit, status,
                    [&](const Json&, float x, float y) {
                        ++picks;
                        return helpers.pick(camera, size, {x, y}, selected, true);
                    });
        const auto preview = scene.preview_document(tools.preview(scene.snapshot()));
        tools.draw(preview, camera, selected, origin, size, can_edit);
        ImGui::End();
        ImGui::Render();
    };
    const auto handle = [&](unsigned index) {
        const auto view = navigation_region_view(scene.effective_document(), id);
        check(bool(view), "Input fixture has no navigation region");
        const auto point = tools.region_handles(camera, *view, size).at(index);
        check(bool(point), "Region handle is not visible in the framed view");
        return ImVec2{origin.x + point->x, origin.y + point->y};
    };
    const auto press = [&](unsigned index) {
        const auto point = handle(index);
        io.AddMousePosEvent(point.x, point.y);
        frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        frame();
        if (!tools.region.active() || tools.move.active() || tools.rig.active())
            throw std::runtime_error("Region handle did not acquire exclusive input at scale " +
                                     std::to_string(scale) + (narrow ? " narrow" : " wide"));
        return point;
    };
    const auto release = [&] {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        frame();
    };
    const auto before = scene.snapshot();
    frame();
    frame();
    check(test::ui_targets.contains("navigation:region-handle:0"),
          "Region resize handles were not drawn");
    selected.clear();
    frame();
    const auto original_view = navigation_region_view(scene.effective_document(), id).value();
    const auto corner = original_view.contours.front()[0];
    const auto outline =
        project_point(camera, {float(corner[0]), float(corner[1]), 0}, size.x, size.y);
    check(bool(outline), "Outline selection point was not projectable");
    io.AddMousePosEvent(origin.x + (*outline)[0], origin.y + (*outline)[1]);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    release();
    if (selected != id || scene.snapshot() != before || picks != 1)
        throw std::runtime_error(
            "Actual Scene outline click failed at scale " + std::to_string(scale) +
            (narrow ? " narrow" : " wide") + ": picks=" + std::to_string(picks) +
            ", selected=" + selected +
            ", direct=" + helpers.pick(camera, size, {(*outline)[0], (*outline)[1]}, {}, true));
    const auto center = original_view.center();
    const auto interior =
        project_point(camera, {float(center[0] + 2), float(center[1] + 2), 0}, size.x, size.y);
    io.AddMousePosEvent(origin.x + (*interior)[0], origin.y + (*interior)[1]);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    release();
    check(selected.empty() && scene.snapshot() == before && picks == 2,
          "A filled region intercepted an ordinary Scene selection click");
    selected = id;
    frame();
    const auto selection_picks = picks;
    auto point = press(0);
    io.AddMousePosEvent(point.x + metre_pixels, point.y);
    frame();
    check(scene.snapshot() == before && std::abs(tools.region.extent() - 12) < .001,
          "Actual pointer resize did not stay in its detached preview");
    release();
    check(!tools.active() &&
              std::abs(scene.entity(id).get<NavigationRegion2D>().width - 12) < .001 &&
              picks == selection_picks,
          "Region resize committed incorrectly or fell through to selection");
    const auto after = scene.snapshot();
    check(scene.undo() && scene.snapshot() == before && scene.redo() && scene.snapshot() == after,
          "Actual pointer resize did not use one Undo/Redo operation");
    scene.undo();
    frame();
    point = press(3); // Negative local-Y edge: dragging down expands centered height.
    io.AddMousePosEvent(point.x, point.y + metre_pixels);
    frame();
    release();
    check(std::abs(scene.entity(id).get<NavigationRegion2D>().height - 12) < .001,
          "Negative height handle used width or the wrong drag direction");
    scene.undo();
    frame();
    point = press(0);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
    io.AddMousePosEvent(point.x + 1.15f * metre_pixels, point.y);
    frame();
    release();
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
    frame();
    check(std::abs(scene.entity(id).get<NavigationRegion2D>().width - 12) < .001,
          "Ctrl did not snap the local extent through actual input");
    scene.undo();
    frame();
    point = press(0);
    io.AddMousePosEvent(point.x + metre_pixels, point.y);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    release();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    frame();
    check(!tools.active() && scene.snapshot() == before, "Escape committed a region preview");
    point = press(0);
    io.AddMousePosEvent(point.x + metre_pixels, point.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
    release();
    check(!tools.active() && scene.snapshot() == before, "Right-click committed a region preview");
    point = press(0);
    io.AddMousePosEvent(point.x + metre_pixels, point.y);
    frame();
    can_edit = false;
    frame();
    release();
    check(!tools.active() && scene.snapshot() == before &&
              !test::ui_targets.contains("navigation:region-handle:0"),
          "Play/modal/focus edit lock left a live or visible resize handle");
    can_edit = true;
    frame();
    point = press(0);
    io.AddMousePosEvent(point.x + metre_pixels, point.y);
    frame();
    size.x += 20;
    frame();
    release();
    size.x -= 20;
    frame();
    check(!tools.active() && scene.snapshot() == before,
          "Viewport resize committed a region preview");
    point = press(0);
    io.AddMousePosEvent(point.x + metre_pixels, point.y);
    frame();
    selected.clear();
    frame();
    release();
    selected = id;
    frame();
    check(!tools.active() && scene.snapshot() == before,
          "Selection change committed a region preview");
    point = press(0);
    io.AddMousePosEvent(point.x + metre_pixels, point.y);
    frame();
    io.AddMousePosEvent(point.x - 4000, point.y);
    frame();
    release();
    check(!tools.active() && scene.snapshot() == before,
          "An invalid release committed the previous valid resize preview");
    check(picks == selection_picks,
          "Region resize/cancellation fell through to ordinary object picking");
    scene.entity(id).set<NodeVisibility>({false});
    frame();
    check(!test::ui_targets.contains("navigation:region-handle:0") &&
              !tools.region.begin(scene, id, 0, 1),
          "Hidden region exposed a handle or acquired resize authority");
    scene.entity(id).remove<NodeVisibility>();
    scene.entity(id).set<NodeSelectability>({false});
    frame();
    check(!test::ui_targets.contains("navigation:region-handle:0") &&
              !tools.region.begin(scene, id, 0, 1),
          "Locked region exposed a handle or acquired resize authority");
    scene.entity(id).remove<NodeSelectability>();
    auto disabled = scene.entity(id).get<NavigationRegion2D>();
    disabled.enabled = false;
    scene.entity(id).set<NavigationRegion2D>(disabled);
    frame();
    check(test::ui_targets.contains("navigation:region-handle:0") &&
              tools.region.begin(scene, id, 0, 1),
          "Disabled region lost editable Scene handles");
    tools.cancel();
    disabled.enabled = true;
    scene.entity(id).set<NavigationRegion2D>(disabled);
    check(scene.snapshot() == before, "Region policy checks changed authored state");
}
