#pragma once
#include "batch_authoring.hpp"
#include "editor_state.hpp"
#include "property_drawer.hpp"
namespace forge::ui {
inline void batch_inspector(Scene& scene, const std::filesystem::path& project,
                            const std::vector<std::string>& ids) {
    ImGui::Text("%zu entities selected", ids.size());
    help("Common reflected properties edit all selected entities in one Undo step. Mixed values "
         "show the primary entity's value until explicitly replaced. Rotation and scale gestures "
         "use individual spatial-root origins; move uses a shared world offset.");
    if (ids.size() > 128) {
        ImGui::TextWrapped(
            "Batch editing supports at most 128 targets per transaction. Reduce the selection.");
        return;
    }
    const auto doc = scene.effective_document();
    std::vector<Json> rows;
    for (const auto& id : ids)
        for (const auto& row : doc.at("entities"))
            if (row.at("id") == id)
                rows.push_back(row);
    if (rows.size() != ids.size())
        return;
    const auto schema = scene.schema();
    for (const auto& type : schema.at("components")) {
        const auto key = type.at("id").get<std::string>();
        if (!std::all_of(rows.begin(), rows.end(),
                         [&](const auto& row) { return row.at("components").contains(key); }))
            continue;
        IdScope component_id(key.c_str());
        const auto title = type.value("display_name", key);
        if (!ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        help("Only components present on every selected entity appear here. Validation rejects the "
             "entire edit if any selected target is incompatible.");
        for (const auto& field : type.at("fields")) {
            const auto name = field.at("id").get<std::string>();
            if (!std::all_of(rows.begin(), rows.end(), [&](const auto& row) {
                    return row.at("components").at(key).contains(name);
                }))
                continue;
            IdScope field_id(name.c_str());
            auto value = rows.back().at("components").at(key).at(name);
            const bool mixed = std::any_of(rows.begin(), rows.end(), [&](const auto& row) {
                return row.at("components").at(key).at(name) != value;
            });
            if (mixed) {
                ImGui::TextDisabled("Mixed values");
                help("The selection has different values. Committing replaces this field on every "
                     "target.");
            }
            try {
                if (property_field(project, field, value)) {
                    apply_authoring(
                        scene,
                        selection_commands(scene, ids, "property.set",
                                           {{"component", key}, {"field", name}, {"value", value}}),
                        scene.revision());
                    return; // The immutable displayed snapshot has been superseded.
                }
            } catch (const std::exception& error) {
                report_error("batch-property", error.what());
            }
        }
    }
    ImGui::TextWrapped("Names, parenting, component add/remove and specialized asset controls "
                       "require a single selection.");
    help("Select one entity for specialized controls. Common supported reflected properties are "
         "editable above.");
}
} // namespace forge::ui
