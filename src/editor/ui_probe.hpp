#pragma once
// Widget/action observation only. No authoring or input side effects; compiled out of
// the shipped editor. The fixture drives the real widgets through ImGui events.
#ifdef FORGE_UI_FIXTURE
#include <imgui.h>
#include <imgui_internal.h>
#include <map>
#include <string>
namespace forge::test {
struct UiTarget {
    ImVec2 minimum, maximum;
    bool enabled;
    ImVec2 clip_minimum{-FLT_MAX, -FLT_MAX}, clip_maximum{FLT_MAX, FLT_MAX};
    ImGuiID id = 0;
};
inline bool observe_ui = false;
inline unsigned play_action_invocations = 0;
inline void observe_action(const std::string& id) {
    if (observe_ui && id == "play")
        ++play_action_invocations;
}
inline std::map<std::string, UiTarget> ui_targets;
inline void observe_item(const std::string& name) {
    if (observe_ui)
        ui_targets[name] = {
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            !(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled),
            ImGui::GetCurrentWindow()->ClipRect.Min,
            ImGui::GetCurrentWindow()->ClipRect.Max,
            ImGui::GetItemID()};
}
inline void observe_tab(const std::string& name) {
    const auto* window = ImGui::GetCurrentWindow();
    if (observe_ui && window->DockIsActive && window->DC.DockTabItemRect.GetWidth() > 0)
        ui_targets[name] = {window->DC.DockTabItemRect.Min, window->DC.DockTabItemRect.Max, true};
}
inline void observe_rect(const std::string& name, ImVec2 minimum, ImVec2 maximum, bool enabled) {
    if (observe_ui)
        ui_targets[name] = {minimum,
                            maximum,
                            enabled && !(ImGui::GetCurrentContext()->LastItemData.ItemFlags &
                                         ImGuiItemFlags_Disabled),
                            ImGui::GetCurrentWindow()->ClipRect.Min,
                            ImGui::GetCurrentWindow()->ClipRect.Max,
                            ImGui::GetItemID()};
}
} // namespace forge::test
#define FORGE_UI_PROBE(name) ::forge::test::observe_item(name)
#define FORGE_UI_ACTION_PROBE(id) ::forge::test::observe_action(id)
#define FORGE_UI_TAB_PROBE(name) ::forge::test::observe_tab(name)
#define FORGE_UI_RECT_PROBE(name, minimum, maximum, enabled)                                       \
    ::forge::test::observe_rect(name, minimum, maximum, enabled)
#else
#define FORGE_UI_PROBE(name) ((void)0)
#define FORGE_UI_ACTION_PROBE(id) ((void)0)
#define FORGE_UI_TAB_PROBE(name) ((void)0)
#define FORGE_UI_RECT_PROBE(name, minimum, maximum, enabled) ((void)0)
#endif
