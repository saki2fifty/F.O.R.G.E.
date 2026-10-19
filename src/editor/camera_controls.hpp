#pragma once
#include "grid.hpp"
#include "widgets.hpp"
namespace forge::ui {
// Submit at the image origin. An actual interactive item owns the drag and
// acquires window focus on RMB/MMB, even when another docked panel had focus.
struct ViewportInput {
    bool hovered = false, active = false, activated = false;
};
// Editor view settings only: no authored Camera or Scene changes.
inline bool camera_view_controls(EditorCamera& camera, GridPlane& plane) {
    bool changed = false;
    int projection = camera.orthographic() ? 1 : 0;
    if (ImGui::Combo("Projection", &projection, "Perspective\0Orthographic\0"))
        changed |= camera.set_projection(projection ? CameraProjection::Orthographic
                                                    : CameraProjection::Perspective);
    FORGE_UI_PROBE("scene:projection");
    help("Perspective gives distant objects a smaller size. Orthographic keeps parallel lines "
         "parallel. Switching matches the size at the view target within the zoom limits.");
    if (camera.orthographic()) {
        auto height = camera.orthographic_height;
        if (ImGui::DragFloat("View height (m)", &height, std::max(.0001f, height * .01f),
                             EditorCamera::minimum_height, EditorCamera::maximum_height, "%.4g",
                             ImGuiSliderFlags_AlwaysClamp) &&
            std::isfinite(height) && height >= EditorCamera::minimum_height &&
            height <= EditorCamera::maximum_height) {
            camera.orthographic_height = height;
            changed = true;
        }
        FORGE_UI_PROBE("scene:orthographic-height");
        help("Full vertical world height visible in the Scene view. The mouse wheel changes "
             "this height while orthographic projection is active.");
    }
    const bool presets = ImGui::BeginMenu("Axis views");
    FORGE_UI_PROBE("scene:axis-views");
    if (presets) {
        struct Preset {
            const char* name;
            unsigned axis;
            int sign;
            GridPlane plane;
        };
        constexpr Preset views[] = {
            {"Front (-Z)", 2, -1, GridPlane::XY}, {"Back (+Z)", 2, 1, GridPlane::XY},
            {"Top (+Y)", 1, 1, GridPlane::XZ},    {"Bottom (-Y)", 1, -1, GridPlane::XZ},
            {"Right (+X)", 0, 1, GridPlane::YZ},  {"Left (-X)", 0, -1, GridPlane::YZ}};
        for (unsigned i = 0; i < std::size(views); ++i) {
            const auto& view = views[i];
            if (ImGui::MenuItem(view.name) &&
                camera.set_projection(CameraProjection::Orthographic)) {
                camera.align(view.axis, view.sign);
                plane = view.plane;
                changed = true;
            }
            FORGE_UI_PROBE("scene:axis-view:" + std::to_string(i));
        }
        ImGui::EndMenu();
    }
    int selected_plane = int(plane);
    if (ImGui::Combo("Grid plane", &selected_plane, "Ground XZ\0Front XY\0Side YZ\0")) {
        plane = GridPlane(selected_plane);
        changed = true;
    }
    FORGE_UI_PROBE("scene:grid-plane");
    help("Reference plane through world zero. Axis views choose a matching plane. Projection, "
         "height and plane are stored with Save view; authored objects stay unchanged.");
    return changed;
}
inline bool camera_controls(EditorCamera& camera, ImVec2 size, bool application_focused,
                            ViewportInput* input = nullptr, bool blocked = false) {
    if (blocked) {
        if (input)
            *input = {};
        ImGui::Dummy(size);
        return false;
    }
    ImGui::InvisibleButton("##viewport-navigation", size,
                           ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle |
                               (input ? ImGuiButtonFlags_MouseButtonLeft : 0));
    const bool hovered = ImGui::IsItemHovered();
    const bool activated = ImGui::IsItemActivated();
    if (activated && application_focused)
        ImGui::SetWindowFocus();
    if (input)
        *input = {hovered, ImGui::IsItemActive(), activated};
    const auto& io = ImGui::GetIO();
    if (application_focused && ImGui::IsItemActive() && ImGui::IsWindowFocused()) {
        // The press may arrive with motion from another panel: start rotation
        // with the next motion sample, avoiding a jump on initial acquisition.
        const float dx = activated ? 0 : io.MouseDelta.x;
        const float dy = activated ? 0 : io.MouseDelta.y;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            if (io.KeyShift)
                camera.pan(dx, dy, size.y);
            else
                camera.orbit(dx, dy);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            camera.look(dx, dy);
            if (!io.WantTextInput)
                camera.fly(
                    float(ImGui::IsKeyDown(ImGuiKey_D)) - float(ImGui::IsKeyDown(ImGuiKey_A)),
                    float(ImGui::IsKeyDown(ImGuiKey_W)) - float(ImGui::IsKeyDown(ImGuiKey_S)),
                    io.DeltaTime, float(ImGui::IsKeyDown(ImGuiKey_Space)) - float(io.KeyShift));
        }
    }
    if (application_focused && hovered && !io.KeyCtrl && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        camera.zoom(io.MouseWheel);
    const bool frame = application_focused && hovered && !io.WantTextInput &&
                       !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                       ImGui::IsKeyPressed(ImGuiKey_F, false);
    help("MMB-drag: orbit. Shift+MMB-drag: pan. RMB-drag: look; hold RMB + WASD to fly, Space up, "
         "Shift down. "
         "Wheel: zoom. F: frame selected. LMB selects the nearest block; drag a selected axis or "
         "center to move. Ctrl snaps; Escape cancels. R: rotate; S: scale, then X/Y/Z constrains. "
         "Object tools are disabled in Play.");
    return frame;
}
} // namespace forge::ui
