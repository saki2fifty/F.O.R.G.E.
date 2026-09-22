#pragma once
// Geometry observation only. No authoring or input side effects; compiled out of
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
};
inline bool observe_ui = false;
inline std::map<std::string, UiTarget> ui_targets;
inline void observe_item(const std::string& name) {
    if (observe_ui)
        ui_targets[name] = {
            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            !(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled)};
}
inline void observe_tab(const std::string& name) {
    const auto* window = ImGui::GetCurrentWindow();
    if (observe_ui && window->DockIsActive && window->DC.DockTabItemRect.GetWidth() > 0)
        ui_targets[name] = {window->DC.DockTabItemRect.Min, window->DC.DockTabItemRect.Max, true};
}
} // namespace forge::test
#define FORGE_UI_PROBE(name) ::forge::test::observe_item(name)
#define FORGE_UI_TAB_PROBE(name) ::forge::test::observe_tab(name)
#else
#define FORGE_UI_PROBE(name) ((void)0)
#define FORGE_UI_TAB_PROBE(name) ((void)0)
#endif
