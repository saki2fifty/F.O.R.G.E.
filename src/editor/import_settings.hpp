#pragma once
#include "editor_state.hpp"
#include "icons.hpp"
#include <forge/import_settings.hpp>
namespace forge::ui {
// Importer settings are not ECS components. Their typed importer schema owns
// defaults/validation; gameplay properties continue to use Flecs Meta.
inline bool import_settings_fields(const ImportSettingsSchema& schema,
                                   ImportSettingsDocument& document, std::string& error) {
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
