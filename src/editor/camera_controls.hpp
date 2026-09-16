#pragma once
#include "camera.hpp"
#include "widgets.hpp"
namespace forge::ui {
// Submit at the image origin. An actual interactive item owns the drag and
// acquires window focus on RMB/MMB, even when another docked panel had focus.
struct ViewportInput {
    bool hovered = false, active = false, activated = false;
};
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
