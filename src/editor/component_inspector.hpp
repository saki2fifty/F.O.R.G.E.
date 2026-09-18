#pragma once
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
namespace forge {
class ComponentInspector {
  public:
    bool add_open = false;
    void edit_property(Scene& scene, const std::string& entity, const std::string& component,
                       const std::string& field, const Json& value) {
        apply(scene, entity, component, field, "property.set", value);
    }
    void add_menu(Scene& scene, const std::string& entity, const Json& effective) {
        if (ui::button(
                "+ Add Component",
                "Search registered components by name or category. Adding is one scene Undo step."))
            add_open = true;
        if (ui::editor_context && ui::editor_context->add_component) {
            add_open = true;
            ui::editor_context->add_component = false;
        }
        if (add_open) {
            ImGui::OpenPopup("Add Component");
            add_open = false;
            search_[0] = 0;
        }
        if (ImGui::BeginPopup("Add Component")) {
            ImGui::InputTextWithHint("##component-search", "Search name or category...", search_,
                                     sizeof(search_));
            ui::help("Results come from the world's registered component schema. Existing "
                     "components are marked Added.");
            const auto schema = scene.schema();
            unsigned count = 0;
            for (const auto& type : schema.at("components")) {
                if (!type.value("optional", false))
                    continue;
                const std::string key = type.at("id");
                const auto label = type.value("display_name", key);
                const auto category = type.value("category", std::string("Components"));
                if (search_key(category + " " + label).find(search_key(search_)) ==
                    std::string::npos)
                    continue;
                ++count;
                const bool present = effective.contains(key);
                ImGui::BeginDisabled(present);
                const auto text = category + " / " + label + (present ? " (Added)" : "");
                if (ImGui::Selectable(text.c_str()))
                    apply(scene, entity, key, "", "component.add", {});
                ui::help(present ? "Already present on this entity, including inherited components."
                                 : "Add the registered defaults; change properties afterward. "
                                   "Scene Undo removes this addition.");
                ImGui::EndDisabled();
            }
            if (!count)
                ImGui::TextUnformatted("No matching components.");
            ImGui::EndPopup();
        }
    }
    void draw(Scene& scene, SceneDocument& project, const std::string& selected) {
        if (selected.empty())
            return;
        const std::string entity =
            selected; // Scope stays stable if Reveal changes editor selection.
        const auto doc = scene.document(), effective = scene.effective_document();
        Json owned, shown;
        for (const auto& e : doc.at("entities"))
            if (e.at("id") == entity)
                owned = e;
        for (const auto& e : effective.at("entities"))
            if (e.at("id") == entity)
                shown = e;
        if (owned.is_null() || shown.is_null())
            return;
        const auto& values = shown.at("components");
        ui::heading("Components", "Behavior attached to this entity. Values come from the Flecs "
                                  "world; inherited values follow their prefab.");
        add_menu(scene, entity, values);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##property-filter", "Search components or properties...", filter_,
                                 sizeof(filter_));
        ui::help("Filter attached component names and property labels. Transform and identity "
                 "remain above this list.");
        const auto schema = scene.schema();
        const bool prefab = owned.contains("prefab_instance") || owned.contains("prefab_member") ||
                            owned.contains("base");
        const auto masks = owned.value("property_overrides", Json::object());
        unsigned visible_components = 0;
        for (const auto& type : schema.at("components")) {
            if (!type.value("optional", false))
                continue;
            const std::string key = type.at("id");
            if (!values.contains(key))
                continue;
            ui::IdScope scope(key.c_str());
            const auto label = type.value("display_name", key);
            const bool component_match =
                search_key(label).find(search_key(filter_)) != std::string::npos;
            bool field_match = false;
            for (const auto& field : type.at("fields"))
                field_match |= search_key(property_label(field)).find(search_key(filter_)) !=
                               std::string::npos;
            if (!component_match && !field_match)
                continue;
            ++visible_components;
            const bool whole = owned.at("components").contains(key);
            const bool partial = masks.contains(key);
            const bool expanded =
                ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            ui::help("Component properties. Right-click this header for Remove or Revert. Revert "
                     "resumes inherited values; it does not delete shared prefab source data.");
            if (ImGui::BeginPopupContextItem("component-actions")) {
                ImGui::BeginDisabled(!whole && !partial);
                if (ImGui::MenuItem(prefab ? "Revert component" : "Remove component"))
                    apply(scene, entity, key, "", "component.revert", {});
                ui::help("Remove this entity's owned component/override. Inherited values become "
                         "visible again. One scene Undo step.");
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            if (!expanded)
                continue;
            ImGui::TextDisabled(
                "%s", prefab ? (whole || partial ? "Overrides present" : "Inherited from prefab")
                             : "Owned component");
            ui::help("Ownership and explicit intent determine overrides, including values equal to "
                     "the prefab source.");
            for (const auto& field : type.at("fields")) {
                const std::string name = field.at("id");
                if (!component_match &&
                    search_key(property_label(field)).find(search_key(filter_)) ==
                        std::string::npos)
                    continue;
                ui::IdScope field_scope(name.c_str());
                auto value = values.at(key).at(name);
                if (property_field(project.project(), field, value))
                    edit_property(scene, entity, key, name, value);
                const bool field_override = whole || (partial && masks.at(key).contains(name));
                if (prefab) {
                    ImGui::TextDisabled("%s", field_override ? "Overridden" : "Inherited");
                    ui::help("Explicit override intent, not a value comparison. Equal-value "
                             "overrides remain overridden.");
                    if (field_override &&
                        (!whole ||
                         name == type.at("fields")[0].at("id").get_ref<const std::string&>())) {
                        ImGui::SameLine();
                        if (ui::button(whole ? "Revert component" : "Revert",
                                       whole ? "This is a whole-component override. Revert resumes "
                                               "every property of this component."
                                             : "Remove only this property's override intent. Scene "
                                               "Undo restores it."))
                            apply(scene, entity, key, whole ? "" : name,
                                  whole ? "component.revert" : "property.revert", {});
                    }
                }
                const auto id = error_key(entity, key, name);
                if (errors_.contains(id))
                    ui::field_error(errors_.at(id));
            }
            const auto id = error_key(entity, key, "");
            if (errors_.contains(id))
                ui::field_error(errors_.at(id));
        }
        if (!visible_components)
            ImGui::TextWrapped(filter_[0]
                                   ? "No matching components or properties. Clear the search to "
                                     "show all attached components."
                                   : "No behavior components attached. Use + Add Component.");
    }

  private:
    static std::string error_key(const std::string& e, const std::string& c, const std::string& f) {
        return e + "/" + c + "/" + f;
    }
    void apply(Scene& scene, const std::string& entity, const std::string& component,
               const std::string& field, const std::string& operation, const Json& value) {
        const auto key = error_key(entity, component, field);
        Json args = {{"entity", entity}, {"component", component}};
        if (!field.empty())
            args["field"] = field;
        if (operation == "property.set")
            args["value"] = value;
        try {
            authoring_command(scene, operation, args);
            errors_.erase(key);
            if (ui::editor_context)
                ui::editor_context->problems.resolve(key);
        } catch (const std::exception& e) {
            if (errors_.size() >= 256)
                errors_.clear();
            errors_[key] = e.what();
            if (ui::editor_context)
                ui::editor_context->problems.report({key,
                                                     "Error",
                                                     e.what(),
                                                     entity,
                                                     {},
                                                     component + "." + field,
                                                     scene.asset_id()});
        }
    }
    char search_[192]{}, filter_[192]{};
    std::map<std::string, std::string> errors_;
};
} // namespace forge
