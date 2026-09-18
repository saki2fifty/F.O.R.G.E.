#pragma once
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
namespace forge::ui {
inline bool tooltips = true;
inline ImVec2 tooltip_position(const ImRect& item, ImVec2 size, const ImRect& screen, float gap) {
    const float x =
        std::clamp(item.Min.x, screen.Min.x, std::max(screen.Min.x, screen.Max.x - size.x));
    const float y =
        std::clamp(item.Min.y, screen.Min.y, std::max(screen.Min.y, screen.Max.y - size.y));
    if (item.Max.y + gap + size.y <= screen.Max.y)
        return {x, item.Max.y + gap};
    if (item.Min.y - gap - size.y >= screen.Min.y)
        return {x, item.Min.y - gap - size.y};
    if (item.Max.x + gap + size.x <= screen.Max.x)
        return {item.Max.x + gap, y};
    if (item.Min.x - gap - size.x >= screen.Min.x)
        return {item.Min.x - gap - size.x, y};
    // Large surfaces (e.g. the viewport image) may leave no external space.
    // Keep the tooltip on screen and on the opposite side from the pointer.
    const auto mouse = ImGui::GetIO().MousePos;
    return {mouse.x < screen.GetCenter().x ? std::max(screen.Min.x, screen.Max.x - size.x)
                                           : screen.Min.x,
            mouse.y < screen.GetCenter().y ? std::max(screen.Min.y, screen.Max.y - size.y)
                                           : screen.Min.y};
}
inline void help(const char* text) {
    if (!tooltips || ImGui::IsAnyMouseDown() ||
        !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        return;
    const auto& style = ImGui::GetStyle();
    const auto* viewport = ImGui::GetWindowViewport();
    const float gap = std::max(6.0f, ImGui::GetFontSize() * 0.6f);
    const ImRect screen{
        ImVec2(viewport->Pos.x + gap, viewport->Pos.y + gap),
        ImVec2(viewport->Pos.x + viewport->Size.x - gap, viewport->Pos.y + viewport->Size.y - gap)};
    const float wrap = std::max(
        1.0f, std::min(ImGui::GetFontSize() * 30, screen.GetWidth() - 2 * style.WindowPadding.x));
    const auto text_size = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const ImVec2 size{std::ceil(text_size.x + 2 * style.WindowPadding.x),
                      std::ceil(text_size.y + 2 * style.WindowPadding.y)};
    const ImRect item{ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
    ImGui::SetNextWindowPos(tooltip_position(item, size, screen, gap));
    ImGui::SetNextWindowSize(size);
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
// Keep toolbar controls at their regular size, adding space around the row
// instead of inflating every button's frame padding.
inline bool begin_toolbar() {
    const auto& style = ImGui::GetStyle();
    const float padding = std::max(6.0f, ImGui::GetFontSize() * 0.75f);
    ImGui::GetCurrentContext()->NextWindowData.MenuBarOffsetMinVal = {style.WindowPadding.x,
                                                                      padding};
    const auto flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, style.Colors[ImGuiCol_MenuBarBg]);
    const bool visible =
        ImGui::BeginViewportSideBar("##FORGE-toolbar", ImGui::GetMainViewport(), ImGuiDir_Up,
                                    ImGui::GetFrameHeight() + 2 * padding, flags);
    ImGui::PopStyleColor();
    ImGui::GetCurrentContext()->NextWindowData.MenuBarOffsetMinVal = {0, 0};
    if (visible && ImGui::BeginMenuBar())
        return true;
    ImGui::End();
    return false;
}
inline void end_toolbar() {
    ImGui::EndMenuBar();
    ImGui::End();
}
inline bool button(const char* label, const char* description) {
    const bool result = ImGui::Button(label);
    help(description);
    return result;
}
inline void heading(const char* label, const char* description) {
    ImGui::SeparatorText(label);
    help(description);
}
inline bool scalar(const char* label, float* value, const char* description) {
    const bool result = ImGui::InputFloat(label, value, 0.1f, 1.0f);
    help(description);
    return result;
}
inline void draft_window_size(ImVec2 preferred) {
    const auto available = ImGui::GetMainViewport()->WorkSize;
    const ImVec2 maximum{std::max(240.f, available.x), std::max(200.f, available.y)};
    ImGui::SetNextWindowSize({std::min(preferred.x, maximum.x), std::min(preferred.y, maximum.y)},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({std::min(360.f, maximum.x), std::min(260.f, maximum.y)},
                                        maximum);
}
inline void initialize_workspace(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock;
    auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
    auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    ImGui::DockBuilderDockWindow("Hierarchy###World", left);
    ImGui::DockBuilderDockWindow("Content", bottom);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Problems###Problems", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Gameplay Code###Native", bottom);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);
    ImGui::DockBuilderFinish(dock);
}
inline float interface_scale = 1.0f;
inline void style(float scale = 1.0f) {
    interface_scale = std::isfinite(scale) ? std::clamp(scale, 0.65f, 2.0f) : 1.0f;
    ImGui::GetStyle() = ImGuiStyle{};
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.HoverDelayNormal = 0.4f;
    s.HoverFlagsForTooltipMouse = ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_DelayNormal |
                                  ImGuiHoveredFlags_NoSharedDelay |
                                  ImGuiHoveredFlags_AllowWhenDisabled;
    s.WindowRounding = 6;
    s.ChildRounding = 5;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.TabRounding = 4;
    s.GrabRounding = 4;
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(9, 6);
    s.ItemSpacing = ImVec2(8, 8);
    s.WindowBorderSize = 1;
    s.FrameBorderSize = 0;
    s.Colors[ImGuiCol_Text] = ImVec4(0.88f, 0.90f, 0.94f, 1);
    s.Colors[ImGuiCol_TextDisabled] = ImVec4(0.47f, 0.51f, 0.58f, 1);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.085f, 0.105f, 1);
    s.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.115f, 0.14f, 1);
    s.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.23f, 0.28f, 0.65f);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.22f, 0.28f, 1);
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.28f, 0.36f, 1);
    s.Colors[ImGuiCol_TitleBg] = s.Colors[ImGuiCol_WindowBg];
    s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.13f, 0.16f, 0.21f, 1);
    s.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.095f, 0.11f, 0.14f, 1);
    s.Colors[ImGuiCol_Header] = ImVec4(0.17f, 0.26f, 0.35f, 1);
    s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.34f, 0.45f, 1);
    s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.40f, 0.53f, 1);
    s.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.21f, 0.28f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = s.Colors[ImGuiCol_HeaderHovered];
    s.Colors[ImGuiCol_ButtonActive] = s.Colors[ImGuiCol_HeaderActive];
    s.Colors[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.75f, 0.95f, 1);
    s.Colors[ImGuiCol_SliderGrab] = s.Colors[ImGuiCol_CheckMark];
    s.Colors[ImGuiCol_Tab] = s.Colors[ImGuiCol_MenuBarBg];
    s.Colors[ImGuiCol_TabSelected] = s.Colors[ImGuiCol_Header];
    s.Colors[ImGuiCol_TabHovered] = s.Colors[ImGuiCol_HeaderHovered];
    s.Colors[ImGuiCol_DockingPreview] = ImVec4(0.40f, 0.75f, 0.95f, 0.5f);
    s.ScaleAllSizes(interface_scale);
    s.FontScaleMain = interface_scale;
}
} // namespace forge::ui
