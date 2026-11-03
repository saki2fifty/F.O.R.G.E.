#pragma once
#include "document.hpp"
#include "editor_state.hpp"
#include "widgets.hpp"
#include <map>

namespace forge::ui {
inline bool input_message_choice(SceneDocument& project, const Json& schema, const Json& components,
                                 const std::string& field, Json& value) {
    std::map<std::string, std::string> choices;
    if (field == "action") {
        const auto input = project.settings().input();
        for (const auto& action : input.actions()) {
            bool control = false;
            for (const auto& context : input.contexts())
                if (context.name == action.context)
                    control = context.control;
            if (action.kind == ActionKind::Digital && !control)
                choices.emplace(action.id.str(), action.name);
        }
    } else {
        for (const auto& type : schema.at("components")) {
            const auto key = type.at("id").get<std::string>();
            if (key != "forge.input_message" && components.contains(key))
                choices.emplace(key, type.value("display_name", key));
        }
    }
    const auto key = value.get<std::string>();
    const auto found = choices.find(key);
    const std::string preview = key.empty()              ? "None"
                                : found == choices.end() ? "Unavailable: " + key
                                                         : found->second;
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    const bool open = ImGui::BeginCombo(field == "action" ? "##action" : "##payload",
                                        preview.c_str(), ImGuiComboFlags_HeightRegular);
    FORGE_UI_PROBE("input-message-choice:" + field);
    if (open) {
        if (ImGui::Selectable("None", key.empty())) {
            value = "";
            changed = true;
        }
        for (const auto& [id, label] : choices) {
            IdScope scope(id.c_str());
            if (ImGui::Selectable(label.c_str(), id == key)) {
                value = id;
                changed = true;
            }
            FORGE_UI_PROBE("input-message-option:" + field + ":" + id);
            help(id.c_str());
            if (id == key)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    help(field == "action"
             ? "Saved Project Settings input action. Sends once per fixed pressed edge."
             : "Attached data component, including inherited values. Each message copies its "
               "current value.");
    return changed;
}
} // namespace forge::ui
