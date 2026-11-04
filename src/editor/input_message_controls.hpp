#pragma once
#include "document.hpp"
#include "editor_state.hpp"
#include "widgets.hpp"
#include <forge/input_message.hpp>
#include <map>

namespace forge::ui {
inline bool input_message_choice(SceneDocument& project, const Json& schema, const Json& components,
                                 const std::string& field, Json& value) {
    if (field == "trigger") {
        const auto selected = value.get<std::uint32_t>();
        const auto* label = input_message_trigger_name(selected);
        bool changed = false;
        ImGui::SetNextItemWidth(-1);
        const bool open = ImGui::BeginCombo("##trigger", label ? label : "Unavailable");
        FORGE_UI_PROBE("input-message-choice:trigger");
        if (open) {
            for (std::uint32_t trigger = 0; trigger <= std::uint32_t(InputMessageTrigger::Changed);
                 ++trigger) {
                if (ImGui::Selectable(input_message_trigger_name(trigger), trigger == selected)) {
                    value = trigger;
                    changed = true;
                }
                FORGE_UI_PROBE("input-message-option:trigger:" + std::to_string(trigger));
            }
            ImGui::EndCombo();
        }
        help("Pressed/Released use digital edges. Changed sends once when the fixed digital or "
             "analog action value changes, including a return to neutral.");
        return changed;
    }
    std::map<std::string, std::string> choices;
    if (field == "action") {
        const auto input = project.settings().input();
        for (const auto& action : input.actions()) {
            bool control = false;
            for (const auto& context : input.contexts())
                if (context.name == action.context)
                    control = context.control;
            const auto trigger = components.at("forge.input_message").value("trigger", 0u);
            if (!control && (action.kind == ActionKind::Digital ||
                             trigger == std::uint32_t(InputMessageTrigger::Changed)))
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
             ? "Saved fixed-tick Project Settings input action. Trigger controls eligible action "
               "kinds."
             : "Attached data component, including inherited values. Each message copies its "
               "current value.");
    return changed;
}
} // namespace forge::ui
