#pragma once
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
namespace forge::ui {
inline bool tooltips = true;
inline void help(const char* text) {
    if (tooltips && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
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
inline void initialize_workspace(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID center = dock;
    auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
    auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
    auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
    ImGui::DockBuilderDockWindow("World", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Native", bottom);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderFinish(dock);
}
inline float interface_scale = 1.0f;
inline void style(float scale = 1.0f) {
    interface_scale = std::isfinite(scale) ? std::clamp(scale, 0.65f, 2.0f) : 1.0f;
    ImGui::GetStyle() = ImGuiStyle{};
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
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
