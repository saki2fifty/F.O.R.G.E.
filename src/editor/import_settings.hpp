#pragma once
#include "editor_state.hpp"
#include "icons.hpp"
#include <forge/import_settings.hpp>
#include <map>
#include <misc/cpp/imgui_stdlib.h>
namespace forge::ui {
// Importer settings are not ECS components. Their typed importer schema owns
// defaults/validation; gameplay properties continue to use Flecs Meta.
inline bool import_settings_fields(
    const ImportSettingsSchema& schema, ImportSettingsDocument& document, std::string& error,
    const std::map<std::string, std::vector<std::string>>* map_choices = nullptr) {
    bool changed = false;
    for (const auto& rule : schema.rules()) {
        IdScope scope(rule.key.c_str());
        const auto effective = schema.effective(document);
        auto value = effective.at(rule.key);
        property_label_row(rule.label.c_str(), rule.help.c_str());
        bool edited = false;
        if (rule.type == ImportSettingType::Boolean) {
            bool v = value.get<bool>();
            edited = ImGui::Checkbox("##value", &v);
            value = v;
        } else if (rule.type == ImportSettingType::Integer) {
            auto v = value.get<std::int64_t>();
            edited = ImGui::InputScalar("##value", ImGuiDataType_S64, &v);
            value = v;
        } else if (rule.type == ImportSettingType::Number) {
            double v = value.get<double>();
            edited = ImGui::InputDouble("##value", &v);
            value = v;
        } else if (rule.type == ImportSettingType::Text) {
            auto text = value.get<std::string>();
            edited = ImGui::InputText("##value", &text);
            value = text;
        } else if (rule.type == ImportSettingType::StringMap && map_choices) {
            ImGui::BeginGroup();
            if (map_choices->empty())
                ImGui::TextWrapped("This shader declares no permutation axes.");
            for (const auto& [axis, choices] : *map_choices) {
                IdScope axis_scope(axis.c_str());
                property_label_row(axis.c_str(),
                                   "Choose one declared value for this shader compilation axis.");
                const auto current = value.value(axis, std::string{});
                if (ImGui::BeginCombo("##axis",
                                      value.contains(axis) ? current.c_str() : "Choose a value")) {
                    for (const auto& choice : choices) {
                        if (ImGui::Selectable(choice.c_str(), choice == current)) {
                            value[axis] = choice;
                            edited = true;
                        }
                        help("Build this declared shader permutation value on the next Import.");
                    }
                    ImGui::EndCombo();
                }
                help("Only the selected combination is compiled. Every declared axis needs a "
                     "value; FORGE never silently chooses the first entry.");
                if (!value.contains(axis) ||
                    std::find(choices.begin(), choices.end(), current) == choices.end())
                    ImGui::TextWrapped("Select a supported value before importing.");
            }
            std::optional<std::string> remove;
            for (const auto& [axis, item] : value.items())
                if (!map_choices->contains(axis)) {
                    IdScope axis_scope(axis.c_str());
                    ImGui::TextWrapped("Removed axis: %s", axis.c_str());
                    help("The draft retains a selection no longer declared by this source. "
                         "Remove it before importing; published assets remain unchanged.");
                    if (button("Remove selection", "Remove this obsolete axis from the draft."))
                        remove = axis;
                }
            if (remove) {
                value.erase(*remove);
                edited = true;
            }
            ImGui::EndGroup();
        } else if (rule.type == ImportSettingType::Choice) {
            const auto current = value.get<std::string>();
            if (ImGui::BeginCombo("##value", current.c_str())) {
                for (const auto& choice : rule.choices)
                    if (ImGui::Selectable(choice.c_str(), choice == current)) {
                        value = choice;
                        edited = true;
                    }
                ImGui::EndCombo();
            }
        } else if (rule.type == ImportSettingType::StringList && !rule.choices.empty()) {
            auto choices = value.get<std::vector<std::string>>();
            ImGui::BeginGroup();
            for (const auto& choice : rule.choices) {
                const auto found = std::find(choices.begin(), choices.end(), choice);
                bool selected = found != choices.end();
                if (ImGui::Checkbox(choice.c_str(), &selected)) {
                    if (selected)
                        choices.push_back(choice);
                    else
                        choices.erase(found);
                    edited = true;
                }
                help(rule.help.c_str());
            }
            ImGui::EndGroup();
            value = choices;
        } else {
            ImGui::TextUnformatted("This setting requires a supported typed editor.");
        }
        help(rule.help.c_str());
        try {
            if (edited) {
                document = schema.edit(document, rule.key, value);
                error.clear();
                changed = true;
            }
            if (document.overrides.contains(rule.key) &&
                button("Use default",
                       "Remove this explicit override and follow the importer default.")) {
                document = schema.edit(document, rule.key, {});
                error.clear();
                changed = true;
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    return changed;
}
} // namespace forge::ui
