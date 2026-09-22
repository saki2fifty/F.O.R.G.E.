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
} // namespace forge::test
#define FORGE_UI_PROBE(name) ::forge::test::observe_item(name)
#else
#define FORGE_UI_PROBE(name) ((void)0)
#endif
