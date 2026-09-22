#pragma once
#include "icons.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <functional>
#include <string>
#include <vector>
namespace forge::ui {
struct EditorAction {
    std::string id, label, shortcut, description;
    bool available = true;
    std::function<void()> execute;
    std::string unavailable_reason;
    std::string help_text() const {
        return description + (available || unavailable_reason.empty()
                                  ? ""
                                  : "\nUnavailable: " + unavailable_reason);
    }
};
class EditorActions {
  public:
    std::vector<EditorAction> entries;
    const EditorAction* find(const std::string& id) const {
        for (const auto& a : entries)
            if (a.id == id)
                return &a;
        return nullptr;
    }
    bool invoke(const std::string& id) const {
        const auto* action = find(id);
        if (!action || !action->available)
            return false;
        action->execute();
        return true;
    }
    bool item(const std::string& id, const char* label = nullptr) const {
        const auto* a = find(id);
        if (!a)
            return false;
        const bool clicked = ImGui::MenuItem(label ? label : a->label.c_str(),
                                             a->shortcut.empty() ? nullptr : a->shortcut.c_str(),
                                             false, a->available);
        FORGE_UI_PROBE("action:" + id);
        help(a->help_text().c_str());
        if (clicked)
            return invoke(id);
        return false;
    }
    bool icon(const std::string& id, Icon symbol, bool selected = false) const {
        const auto* a = find(id);
        if (!a)
            return false;
        ImGui::BeginDisabled(!a->available);
        const auto description = a->label +
                                 (a->shortcut.empty() ? "" : "\nShortcut: " + a->shortcut) + "\n" +
                                 a->help_text();
        const bool clicked =
            icon_button(("##" + id).c_str(), symbol, description.c_str(), selected);
        FORGE_UI_PROBE("icon:" + id);
        ImGui::EndDisabled();
        return clicked && invoke(id);
    }
    bool button(const std::string& id, const char* label = nullptr) const {
        const auto* a = find(id);
        if (!a)
            return false;
        ImGui::BeginDisabled(!a->available);
        const bool clicked = ui::button(label ? label : a->label.c_str(), a->help_text().c_str());
        ImGui::EndDisabled();
        if (clicked)
            return invoke(id);
        return false;
    }
};
// Wrap only at a complete action boundary. Every action also has a menu/palette route.
inline void next_toolbar_item(float width) {
    const auto& s = ImGui::GetStyle();
    if (ImGui::GetContentRegionAvail().x > width + s.ItemSpacing.x)
        ImGui::SameLine();
}
} // namespace forge::ui
