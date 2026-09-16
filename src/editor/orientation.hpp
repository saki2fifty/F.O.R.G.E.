#pragma once
#include "camera.hpp"
#include "widgets.hpp"
namespace forge::ui {
inline ImU32 axis_color(unsigned axis) {
    const ImU32 colors[] = {IM_COL32(255, 95, 95, 255), IM_COL32(105, 235, 125, 255),
                            IM_COL32(95, 160, 255, 255)};
    return colors[axis];
}
struct OrientationGizmo {
    bool visible = true;
    bool dragging = false, moved = false;
    int pressed = -1;
    ImVec2 center{}, corner{}, extent{}, press{}, captured_size{};
    std::array<ImVec2, 6> ends{};
    std::array<int, 6> order{};
    float radius = 0;
    void geometry(const EditorCamera& camera, ImVec2 origin, ImVec2 size) {
        const float edge = std::min(104 * interface_scale, std::min(size.x, size.y));
        extent = {edge, edge};
        corner = {origin.x + size.x - edge, origin.y};
        center = {corner.x + edge * .5f, corner.y + edge * .5f};
        radius = edge * .12f;
        const auto r = camera.right(), u = camera.up(), f = camera.forward();
        for (int i = 0; i < 6; ++i) {
            const float sign = i % 2 == 0 ? 1.0f : -1.0f;
            ends[i] = {center.x + r[i / 2] * sign * edge * .31f,
                       center.y - u[i / 2] * sign * edge * .31f};
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return f[a / 2] * (a % 2 ? -1 : 1) > f[b / 2] * (b % 2 ? -1 : 1);
        });
    }
    bool input(EditorCamera& camera, ImVec2 origin, ImVec2 size, bool allowed) {
        if (!visible || !allowed || size.x < 70 * interface_scale ||
            size.y < 70 * interface_scale) {
            dragging = false;
            return false;
        }
        if (dragging &&
            (std::abs(size.x - captured_size.x) > .5f || std::abs(size.y - captured_size.y) > .5f))
            dragging = false;
        geometry(camera, origin, size);
        const auto mouse = ImGui::GetIO().MousePos;
        const bool inside =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            mouse.x >= corner.x && mouse.y >= corner.y && mouse.x < corner.x + extent.x &&
            mouse.y < corner.y + extent.y;
        ImGui::SetCursorScreenPos(corner);
        ImGui::InvisibleButton("##orientation", extent);
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemActivated()) {
            ImGui::SetWindowFocus();
            dragging = true;
            moved = false;
            press = mouse;
            captured_size = size;
            pressed = -1;
            for (int i : order)
                if (std::hypot(mouse.x - ends[i].x, mouse.y - ends[i].y) <= radius)
                    pressed = i;
        }
        if (dragging && !ImGui::IsWindowFocused())
            dragging = false;
        if (dragging) {
            moved |= std::hypot(mouse.x - press.x, mouse.y - press.y) > 3 * interface_scale;
            if (active && moved)
                camera.orbit(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
            if (!ImGui::IsMouseDown(0)) {
                if (!moved && pressed >= 0 && inside) {
                    const unsigned axis = unsigned(pressed / 2);
                    int sign = pressed % 2 ? -1 : 1;
                    if (std::abs(camera.forward()[axis] + sign) < .000001f)
                        sign = -sign;
                    camera.align(axis, sign);
                }
                dragging = false;
            }
        }
        help("World orientation (Y up). Click + or - X/Y/Z for an axis view; click the current "
             "axis to flip sides; drag to orbit. "
             "Perspective projection. Camera changes do not edit the scene.");
        geometry(camera, origin, size);
        return inside || active || dragging;
    }
    void draw(const EditorCamera& camera, ImVec2 origin, ImVec2 size) {
        if (!visible || size.x < 70 * interface_scale || size.y < 70 * interface_scale)
            return;
        geometry(camera, origin, size);
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        draw->AddCircleFilled(center, extent.x * .48f, IM_COL32(20, 27, 36, 210));
        const char* labels[] = {"X", "-X", "Y", "-Y", "Z", "-Z"};
        for (int i : order) {
            const auto color = axis_color(unsigned(i / 2));
            draw->AddLine(center, ends[i], color, 2 * interface_scale);
            draw->AddCircleFilled(ends[i], radius, i % 2 ? IM_COL32(35, 43, 54, 255) : color);
            draw->AddCircle(ends[i], radius, color);
            const auto text = ImGui::CalcTextSize(labels[i]);
            draw->AddText({ends[i].x - text.x * .5f, ends[i].y - text.y * .5f},
                          i % 2 ? color : IM_COL32(20, 25, 32, 255), labels[i]);
        }
        draw->PopClipRect();
    }
};
} // namespace forge::ui
