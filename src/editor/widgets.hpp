#pragma once
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
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderFinish(dock);
}
inline void style() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 4;
    s.FrameRounding = 3;
    s.FramePadding = ImVec2(7, 5);
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.08f, 1);
    s.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.28f, 0.31f, 1);
    s.Colors[ImGuiCol_Button] = ImVec4(0.1f, 0.3f, 0.34f, 1);
    s.Colors[ImGuiCol_CheckMark] = ImVec4(1, 0.58f, 0.22f, 1);
    s.Colors[ImGuiCol_DockingPreview] = ImVec4(1, 0.58f, 0.22f, 0.5f);
}
} // namespace forge::ui
