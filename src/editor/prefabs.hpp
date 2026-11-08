#pragma once
#include "../json_value_equal.hpp"
#include "../prefab_component_intent.hpp"
#include "../prefab_composition.hpp"
#include "../prefab_member_access.hpp"
#include "component_choices.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include "sprite_inspector.hpp"
#include "widgets.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <set>
namespace forge {
inline const char* prefab_component_label(const std::string& key) {
    if (key == "forge.navigation_agent")
        return "Navigation Agent";
    if (key == "forge.navigation_obstacle")
        return "Navigation Obstacle";
    if (key == "forge.navigation_behavior")
        return "Navigation Behavior";
    if (key == "forge.navigation_avoidance")
        return "Navigation Avoidance";
    if (key == "forge.navigation_surface")
        return "Navigation Surface";
    if (key == "forge.animator")
        return "Animator";
    if (key == "forge.audio_source")
        return "Audio Source";
    if (key == "forge.audio_listener")
        return "Audio Listener";
    if (key == "forge.local_translation")
        return "Translation";
    if (key == "forge.local_rotation")
        return "Rotation";
    if (key == "forge.local_scale")
        return "Scale";
    if (key == "forge.tint")
        return "Color";
    if (key == "forge.primitive")
        return "Primitive";
    if (key == "forge.character_controller")
        return "Character Controller";
    if (key == "forge.character_controller_2d")
        return "Character Controller 2D";
    if (key == "forge.physics_body")
        return "Physics Body";
    if (key == "forge.box_collider")
        return "Box Collider";
    if (key == "forge.sphere_collider")
        return "Sphere Collider";
    if (key == "forge.capsule_collider")
        return "Capsule Collider";
    if (key == "forge.cylinder_collider")
        return "Cylinder Collider";
    if (key == "forge.asset_collider")
        return "Asset Collider";
    return key.c_str();
}
inline std::string prefab_member_label(const Json& document, const std::string& id) {
    for (const auto& member : document.at("members"))
        if (member.at("id") == id)
            return member.at("name");
    return "Missing member";
}
class PrefabEditor {
    friend struct PrefabAutomation;

  public:
    ui::SpriteFrameField sprite_frames;
    std::function<std::optional<Json>(const ui::SpriteFrameContext&)> atlas_frames;
    std::function<bool(const Json& components, Json& layer)> tile_map_layer;
    bool dirty() const { return open_ && !detail::json_value_equal(draft_, baseline_); }
    bool is_open() const { return open_; }
    bool variant() const { return PrefabDocument::is_variant(draft_); }
    bool inherited_member(const std::string& member) const {
        return variant() && !detail::owned_prefab_member(draft_, member);
    }
    Json source_view(const Json& schema = Json::object()) const {
        if (!variant())
            return draft_;
        auto sources = baseline_bases_;
        const auto asset = draft_.at("asset_id").get<AssetId>();
        sources[asset] = draft_;
        return resolve_prefab_sources(sources, schema).at(asset);
    }
    Json migration_document() const {
        if (!open_)
            throw std::runtime_error("Open a prefab source before preparing its migration");
        return draft_;
    }
    Json migration_document(const SceneDocument& project) const {
        if (project_ != project.project())
            throw std::runtime_error("Prefab source belongs to another project");
        return migration_document();
    }
    void accept_migration(const Json& before, const Json& candidate,
                          const Json& schema = Json::object()) {
        if (!open_ || !detail::json_value_equal(before, draft_))
            throw std::runtime_error(
                "Prefab draft changed during migration; prepare a fresh candidate");
        edit_draft(candidate, schema);
    }
    void edit_draft(const Json& candidate, const Json& schema = Json::object(),
                    const PrefabSources* supporting = nullptr) {
        if (!open_)
            throw std::runtime_error("Open a prefab source before editing its draft");
        PrefabDocument::validate(candidate);
        if (candidate.dump(2).size() > PrefabLibrary::max_source_bytes)
            throw std::runtime_error("Prefab source exceeds 8 MiB");
        if (candidate.at("asset_id") != draft_.at("asset_id") ||
            candidate.at("revision") != draft_.at("revision") ||
            ((PrefabDocument::is_variant(candidate) || PrefabDocument::is_variant(draft_)) &&
             candidate.value("base", Json()) != draft_.value("base", Json())))
            throw std::runtime_error(
                "Edits cannot change prefab source/base identity or publication revision");
        auto sources = supporting ? *supporting : baseline_bases_;
        const auto asset = candidate.at("asset_id").get<AssetId>();
        sources[asset] = candidate;
        const auto view = resolve_prefab_sources(sources, schema).at(asset);
        draft_ = candidate;
        if (supporting)
            baseline_bases_ = *supporting;
        if (std::none_of(view.at("members").begin(), view.at("members").end(),
                         [&](const Json& member) { return member.at("id") == member_; })) {
            member_ = draft_.at("root").get<std::string>();
            if (ui::editor_context &&
                ui::editor_context->selection.kind() == ui::SelectionKind::PrefabMember &&
                ui::editor_context->selection.asset() == draft_.at("asset_id").get<AssetId>())
                ui::editor_context->selection.select_member(draft_.at("asset_id").get<AssetId>(),
                                                            member_);
        }
    }
    void edit_draft(const Json& candidate, Scene& scene, SceneDocument& project) {
        project.check_ownership();
        if (!open_ || project_ != project.project())
            throw std::runtime_error("Open this project's prefab source before editing");
        const auto supporting = project.prefabs().base_sources(scene, baseline_, &candidate);
        for (const auto& [asset, reviewed] : baseline_bases_)
            if (const auto at = supporting.find(asset);
                at != supporting.end() && !detail::json_value_equal(at->second, reviewed))
                throw std::runtime_error("Prefab supporting source changed; review sources again");
        edit_draft(candidate, scene.schema(), &supporting);
    }
    void set_nested_prefab(Scene& scene, SceneDocument& project, PrefabMemberId mount,
                           AssetId asset) {
        const auto view = source_view(scene.schema());
        if (std::none_of(view.at("members").begin(), view.at("members").end(),
                         [&](const Json& row) { return row.at("id") == Json(mount); }))
            throw std::runtime_error("Choose an available source member as the nested mount");
        edit_draft(PrefabDocument(draft_).with_nested_prefab(mount, asset).source, scene, project);
    }
    void revert_nested_prefab(Scene& scene, SceneDocument& project, PrefabMemberId mount) {
        edit_draft(PrefabDocument(draft_).revert_nested_prefab(mount).source, scene, project);
    }
    PrefabMemberId add_child(PrefabMemberId parent, const Json& schema,
                             const std::string& name = "Child") {
        const auto view = source_view(schema);
        if (std::none_of(view.at("members").begin(), view.at("members").end(),
                         [&](const Json& row) { return row.at("id") == Json(parent); }))
            throw std::runtime_error("Choose an available parent before adding a source child");
        Json components = Json::object();
        for (const auto& type : schema.at("components"))
            if (!type.value("optional", false)) {
                const auto key = type.at("id").get<std::string>();
                for (const auto& field : type.at("fields"))
                    components[key][field.at("id").get<std::string>()] = field.at("default");
                if (type.value("custom", false))
                    components[key]["$forge"] = type.at("admission");
            }
        components["forge.local_translation"]["y"] = 1;
        const auto id = PrefabMemberId::generate();
        edit_draft(PrefabDocument(draft_)
                       .with_added_member({{"id", id},
                                           {"name", name},
                                           {"parent", parent},
                                           {"components", std::move(components)}})
                       .source,
                   schema);
        return id;
    }
    void set_member_parent(PrefabMemberId member, PrefabMemberId parent, const Json& schema) {
        edit_draft(PrefabDocument(draft_).with_member_parent(member, parent).source, schema);
    }
    void set_member_spatial(PrefabMemberId member, const Json& binding, const Json& schema) {
        edit_draft(PrefabDocument(draft_).with_member_spatial(member, binding).source, schema);
    }
    void set_member_removed(PrefabMemberId member, bool removed, const Json& schema) {
        edit_draft(PrefabDocument(draft_).with_member_removed(member, removed).source, schema);
    }
    void set_member_order(PrefabMemberId parent, const std::vector<PrefabMemberId>& prefix,
                          const Json& schema) {
        edit_draft(PrefabDocument(draft_).with_member_order(parent, prefix).source, schema);
    }
    void revert_member_structure(PrefabMemberId member, const std::string& field,
                                 const Json& schema) {
        edit_draft(PrefabDocument(draft_).revert_member_structure(member, field).source, schema);
    }
    bool close_cancelled = false;
    void request_close() {
        if (dirty())
            close_requested_ = true;
        else
            finish_close();
    }
    void request_save() { save_requested_ = true; }
    void rename_member(const std::string& name) { selected_intent()["name"] = name; }
    void set_property(const std::string& component, const std::string& field, const Json& value) {
        if (!inherited_member(member_) || selected_intent().at("components").contains(component))
            selected_intent().at("components").at(component)[field] = value;
        else if (component.starts_with("forge.local_")) {
            const auto view = source_view();
            for (const auto& row : view.at("members"))
                if (row.at("id") == member_) {
                    auto channel = row.at("components").at(component);
                    channel[field] = value;
                    set_component(component, channel);
                    return;
                }
        } else {
            const auto view = source_view();
            const auto& intent = selected_intent();
            const bool existing = intent.contains("property_overrides") &&
                                  intent.at("property_overrides").contains(component);
            auto& mask = selected_intent()["property_overrides"][component];
            for (const auto& row : view.at("members"))
                if (row.at("id") == member_ && row.at("components").contains(component) &&
                    row.at("components").at(component).contains("$forge")) {
                    if (existing &&
                        (!mask.is_object() || mask.value("$forge", Json()) !=
                                                  row.at("components").at(component).at("$forge")))
                        throw std::runtime_error("Variant override schema is unavailable; migrate "
                                                 "before editing its properties");
                    mask["$forge"] = row.at("components").at(component).at("$forge");
                }
            mask[field] = value;
        }
    }
    void set_component(const std::string& component, const Json& value) {
        auto& row = selected_intent();
        if (inherited_member(member_) && draft_.at("version").get<unsigned>() >= 5)
            detail::clear_prefab_component_suppression(row, component);
        row["components"][component] = value;
        if (row.contains("property_overrides"))
            row["property_overrides"].erase(component);
        prune_intent();
    }
    void revert_name() {
        if (inherited_member(member_)) {
            selected_intent().erase("name");
            prune_intent();
        }
    }
    void revert_component(const std::string& component) {
        auto& row = selected_intent();
        if (inherited_member(member_) && draft_.at("version").get<unsigned>() >= 5)
            detail::clear_prefab_component_suppression(row, component);
        row["components"].erase(component);
        if (inherited_member(member_) && row.contains("property_overrides"))
            row["property_overrides"].erase(component);
        prune_intent();
    }
    void suppress_component(const std::string& component, const Json& schema) {
        const auto view = source_view(schema);
        const auto row = std::find_if(view.at("members").begin(), view.at("members").end(),
                                      [&](const Json& value) { return value.at("id") == member_; });
        if (row == view.at("members").end() || !row->at("components").contains(component))
            throw std::runtime_error("Select a current variant component to suppress");
        for (const auto& type : schema.at("components"))
            if (type.at("id") == component && type.value("custom", false) &&
                row->at("components").at(component).value("$forge", Json()) != type.at("admission"))
                throw std::runtime_error("Migrate unavailable component values before suppression");
        edit_draft(
            PrefabDocument(draft_)
                .with_component_suppression(PrefabMemberId::parse(member_), component, true, schema)
                .source,
            schema);
    }
    void revert_property(const std::string& component, const std::string& field) {
        if (!inherited_member(member_))
            throw std::runtime_error("Only variant properties inherit from a base source");
        auto& row = selected_intent();
        if (row.contains("property_overrides") && row["property_overrides"].contains(component)) {
            auto& mask = row["property_overrides"][component];
            mask.erase(field);
            if (mask.empty() || (mask.size() == 1 && mask.contains("$forge")))
                row["property_overrides"].erase(component);
        }
        prune_intent();
    }
    bool resolve_close(ui::DraftResolution choice, Scene& scene, SceneDocument& project) {
        if (choice == ui::DraftResolution::Cancel) {
            close_requested_ = false;
            pending_asset_ = {};
            close_cancelled = true;
            return false;
        }
        if (choice == ui::DraftResolution::Save && !publish(scene, project))
            return false;
        draft_ = baseline_;
        close_requested_ = false;
        finish_close();
        return true;
    }
    bool publish(Scene& scene, SceneDocument& project) {
        try {
            project.check_ownership();
            project.prefabs().publish(scene, baseline_, draft_, baseline_bases_);
            baseline_ = project.prefabs().source(baseline_.at("asset_id").get<AssetId>());
            draft_ = baseline_;
            baseline_bases_ = project.prefabs().base_sources(scene, baseline_);
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void edit_source(Scene& scene, SceneDocument& project, AssetId asset) {
        if (open_ && project_ == project.project() && baseline_.at("asset_id") == Json(asset)) {
            focus_requested_ = true;
            if (ui::editor_context)
                ui::editor_context->selection.select_member(asset, member_);
            return;
        }
        if (dirty()) {
            pending_asset_ = asset;
            close_requested_ = true;
            return;
        }
        auto source = project.prefabs().source(asset);
        auto bases = project.prefabs().base_sources(scene, source);
        baseline_ = source;
        draft_ = std::move(source);
        baseline_bases_ = std::move(bases);
        member_ = draft_.at("root");
        selected_ = asset;
        ++source_session_;
        project_ = project.project();
        content_project_ = project_;
        open_ = true;
        focus_requested_ = true;
        error_.clear();
        if (ui::editor_context)
            ui::editor_context->selection.select_member(asset, member_);
    }
    void rebase_draft(Scene& scene, SceneDocument& project) {
        project.check_ownership();
        if (!open_ || project_ != project.project())
            throw std::runtime_error(
                "Open this project's prefab source before reviewing supporting sources");
        if (!detail::json_value_equal(
                project.prefabs().source(baseline_.at("asset_id").get<AssetId>()), baseline_))
            throw std::runtime_error(
                "Prefab source changed on disk; reopen it before reviewing supporting sources");
        project.prefabs().refresh(scene);
        auto bases = project.prefabs().base_sources(scene, baseline_, &draft_);
        auto sources = bases;
        const auto asset = draft_.at("asset_id").get<AssetId>();
        sources[asset] = draft_;
        (void)resolve_prefab_sources(sources, scene.schema());
        baseline_bases_.swap(bases);
        error_.clear();
    }
    void content(Scene& scene, SceneDocument& project, std::string& selection, bool locked) {
        if (content_project_ != project.project()) {
            selected_ = {};
            content_project_ = project.project();
        }
        if (ui::editor_context) {
            const auto& current = ui::editor_context->selection;
            selected_ = current.kind() == ui::SelectionKind::Asset &&
                                project.prefabs().records().contains(current.asset())
                            ? current.asset()
                            : AssetId{};
        }
        ImGui::BeginDisabled(locked);
        auto run = [&](auto action) {
            try {
                project.check_ownership();
                action();
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        };
        ImGui::InputTextWithHint("##prefab-path", "Assets/Name.prefab.json", path_, sizeof(path_));
        FORGE_UI_PROBE("prefab:path");
        ui::help("Project-relative destination for Create or Duplicate. Existing files are never "
                 "overwritten.");
        ImGui::BeginDisabled(selection.empty());
        if (ui::button("Create from selection", "Save the selected subtree as a new prefab asset. "
                                                "Original scene objects stay unchanged."))
            run([&] {
                selected_ = project.prefabs().create(scene, create_prefab_source(scene, selection),
                                                     std::filesystem::u8path(path_));
                if (ui::editor_context)
                    ui::editor_context->selection.select_asset(selected_);
            });
        ImGui::EndDisabled();
        if (ui::button(
                "Refresh prefabs",
                "Validate external source edits and reconcile instances. Failed candidates keep "
                "the previous revision. Successful source changes clear scene history."))
            run([&] { project.prefabs().refresh(scene); });
        if (!ui::editor_context)
            for (const auto& [id, record] : project.prefabs().records()) {
                if (ImGui::Selectable(path_text(record.source).c_str(), selected_ == id))
                    selected_ = id;
                ui::help(id.str().c_str());
            }
        ImGui::BeginDisabled(!selected_);
        if (ui::button(
                "Instantiate",
                "Add a linked prefab instance with fresh entity identities. One scene Undo step."))
            run([&] {
                selection = instantiate_prefab(scene, selected_);
                if (ui::editor_context)
                    ui::editor_context->selection.select_entity(selection);
            });
        ImGui::SameLine();
        if (ui::button("Edit source", "Open the reusable prefab definition. Published edits affect "
                                      "all instances without overriding their explicit edits."))
            run([&] { edit_source(scene, project, selected_); });
        if (ui::button("Duplicate asset", "Create an independent prefab at the destination path "
                                          "with a new asset identity. Variant copies retain their "
                                          "base and inherited member identities."))
            run([&] {
                selected_ =
                    project.prefabs().duplicate(scene, selected_, std::filesystem::u8path(path_));
                if (ui::editor_context)
                    ui::editor_context->selection.select_asset(selected_);
            });
        if (ui::button("Create variant", "Create a new Prefab inheriting this asset's structure "
                                         "and values. Explicit overrides belong to the variant."))
            run([&] {
                selected_ = project.prefabs().create_variant(scene, selected_,
                                                             std::filesystem::u8path(path_));
                if (ui::editor_context)
                    ui::editor_context->selection.select_asset(selected_);
            });
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!error_.empty()) {
            ImGui::TextWrapped("%s", error_.c_str());
            ui::help("No failed source candidate is published. Correct the source or destination "
                     "and retry.");
        }
    }
    void inspector(Scene& scene, SceneDocument& project, const Json& item) {
        const Json* root = &item;
        const auto doc = scene.document();
        if (item.contains("prefab_member")) {
            root = nullptr;
            for (const auto& r : doc.at("entities"))
                if (r.at("id") == item["prefab_member"]["root"])
                    root = &r;
        }
        if (!root || !root->contains("prefab_instance"))
            return;
        ui::heading("Prefab instance",
                    "Inherited values follow the source. Explicit overrides remain even when equal "
                    "to the source. Revert is undoable in this scene.");
        const auto& p = root->at("prefab_instance");
        ImGui::Text("Revision %llu | %s",
                    static_cast<unsigned long long>(p.at("revision").get<std::uint64_t>()),
                    p.value("status", "unknown").c_str());
        ui::help(p.at("asset").get<std::string>().c_str());
        if (item.value("missing_member", false)) {
            ImGui::TextWrapped(
                "This source member is unavailable. Its identity and overrides are preserved.");
            ui::help("Restore the same source member identity to reconnect. A newly created member "
                     "has a different identity.");
        }
        if (ui::button(
                "Open prefab source",
                "Edit this shared asset. Scene Undo does not undo published source changes.")) {
            try {
                edit_source(scene, project, p.at("asset").get<AssetId>());
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        ImGui::BeginDisabled(!item.contains("prefab_instance") || !project.on_disk() || dirty());
        if (ui::button(
                "Apply instance overrides...",
                "Review admitted overrides across this instance. Save the prefab and current scene "
                "together; one Scene Undo/Redo step. Opaque values and attachments stay in the "
                "scene.")) {
            try {
                apply_candidate_ = project.prefabs().prepare_apply(scene, item.at("id"));
                apply_revision_ = scene.revision();
                apply_generation_ = project.generation();
                apply_error_.clear();
                ImGui::OpenPopup("Apply to Prefab");
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        ImGui::EndDisabled();
        ui::help(
            "Select the instance root and save the scene first. Publish or discard an open prefab "
            "draft before Apply. Apply uses the published source, never an unsaved draft.");
        if (ImGui::BeginPopupModal("Apply to Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Apply %zu admitted override(s) to the shared prefab?",
                               apply_candidate_.overrides);
            ui::help("Known components, supported property overrides and root names. Independent "
                     "local TRS "
                     "channels retain their intent. Unknown data is preserved on the instance.");
            ImGui::TextWrapped("This saves BOTH the prefab and this scene, including other unsaved "
                               "scene changes.");
            ui::help("All open instances follow changed source values unless overridden. Scene "
                     "Undo/Redo "
                     "writes both files again. Direct source publication remains a separate "
                     "history boundary.");
            ImGui::TextWrapped("Scene: %s", project.path().filename().string().c_str());
            ui::help(path_text(project.path()).c_str());
            const bool stale =
                apply_revision_ != scene.revision() || apply_generation_ != project.generation();
            if (stale)
                ImGui::TextWrapped("Scene changed. Cancel and review Apply again.");
            ImGui::BeginDisabled(stale);
            if (ui::button("Apply and save both", "Validate all affected instances, then commit "
                                                  "the recoverable two-file pair.")) {
                try {
                    project.apply_prefab(apply_candidate_, apply_revision_);
                    apply_candidate_ = {};
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    apply_error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Keep source, scene, overrides and history unchanged.")) {
                apply_candidate_ = {};
                ImGui::CloseCurrentPopup();
            }
            if (!apply_error_.empty())
                ui::field_error(apply_error_);
            ImGui::EndPopup();
        }
        if (!error_.empty())
            ui::field_error(error_);
        const std::string entity = item.at("id");
        if (item.value("name_override", false) &&
            ui::button("Revert name",
                       "Follow the source root name again. Undo restores your instance name.")) {
            try {
                authoring_command(scene, "prefab.revert_name", {{"entity", entity}});
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        if (!ImGui::TreeNode("All override operations")) {
            ui::help("Advanced overview of component/property revert actions. Common Revert "
                     "controls are beside their properties above.");
            return;
        }
        ui::help("Revert explicit instance overrides without changing the shared source asset.");
        const auto schema = scene.schema();
        for (const auto& component : schema.at("components")) {
            const std::string key = component.at("id");
            const bool whole = item.at("components").contains(key);
            const auto masks = item.value("property_overrides", Json::object());
            if (!whole && !masks.contains(key))
                continue;
            ImGui::PushID(key.c_str());
            if (ui::button((std::string("Revert ") + prefab_component_label(key)).c_str(),
                           "Remove this component's instance override intent. Other components are "
                           "unchanged. Scene Undo restores it.")) {
                try {
                    authoring_command(scene, "component.revert",
                                      {{"entity", entity}, {"component", key}});
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            if (!whole)
                for (const auto& [field, value] : masks.at(key).items()) {
                    (void)value;
                    ImGui::PushID(field.c_str());
                    if (ui::button(("Revert " + field).c_str(),
                                   "Remove only this field's override and follow the source value "
                                   "again.")) {
                        try {
                            authoring_command(
                                scene, "property.revert",
                                {{"entity", entity}, {"component", key}, {"field", field}});
                        } catch (const std::exception& e) {
                            error_ = e.what();
                        }
                    }
                    ImGui::PopID();
                }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    void draw(Scene& scene, SceneDocument& project, bool locked) {
        if (project_ != project.project())
            open_ = false;
        if (!open_)
            return;
        ui::draft_window_size({620 * ui::interface_scale, 620 * ui::interface_scale});
        if (auto* scene_window = ImGui::FindWindowSettingsByID(ImHashStr("###Scene")))
            ImGui::SetNextWindowDockID(scene_window->DockId, ImGuiCond_FirstUseEver);
        if (focus_requested_) {
            ImGui::SetNextWindowFocus();
            focus_requested_ = false;
        }
        bool visible = true;
        const auto title = std::string(dirty() ? "* " : "") + "Prefab source###Prefab source";
        const bool expanded = ImGui::Begin(title.c_str(), &visible);
        if (!visible)
            request_close();
        if (ui::editor_context)
            ui::editor_context->task.focus(ui::DocumentTask::Prefab);
        if (save_requested_ && !locked) {
            save_requested_ = false;
            publish(scene, project);
        }
        if (expanded) {
            ImGui::TextWrapped(
                "%s | Publish saves this asset. Scene Undo does not edit this draft.",
                dirty() ? "Unsaved draft" : "Published");
            ui::help("This is an independent prefab source document. Ctrl+S publishes when this "
                     "task is active; Undo/Redo are scene-only and disabled for this task.");
            const auto edited = baseline_.at("asset_id").get<AssetId>();
            if (project.prefabs().records().contains(edited))
                ImGui::TextWrapped(
                    "Source: %s", path_text(project.prefabs().records().at(edited).source).c_str());
            ui::help("The file being edited in this source window.");
            ImGui::TextWrapped(
                "Edit the reusable source here. Publishing updates non-overridden instance values "
                "and "
                "clears scene Undo/Redo. Scene Undo never reverses a published asset edit.");
            ui::help("Closing an unsaved draft asks to Publish, Discard or Cancel.");
            ImGui::BeginDisabled(locked);
            try {
                const auto schema = scene.schema();
                auto sources = baseline_bases_;
                sources[edited] = draft_;
                const auto resolved = variant() || draft_.at("version").get<unsigned>() >= 7
                                          ? resolve_prefab_sources(sources, schema)
                                          : PrefabSources{};
                auto view = variant() ? resolved.at(edited) : draft_;
                if (variant()) {
                    const auto base = draft_.at("base").get<AssetId>();
                    const auto found = project.prefabs().records().find(base);
                    const auto label = found == project.prefabs().records().end()
                                           ? base.str()
                                           : path_text(found->second.source);
                    ImGui::TextWrapped(
                        "Variant base: %s | Local structure and values stay in this source.",
                        label.c_str());
                    if (ui::button("Rebase draft",
                                   "Refresh published sources and review the new base values while "
                                   "retaining this draft’s explicit overrides. Successful source "
                                   "refresh clears Scene history."))
                        rebase_draft(scene, project);
                } else if (!baseline_bases_.empty() &&
                           ui::button("Review supporting sources",
                                      "Refresh published sources while retaining this draft. "
                                      "Successful refresh clears Scene history."))
                    rebase_draft(scene, project);
                for (const auto& m : view.at("members")) {
                    const std::string id = m.at("id");
                    ImGui::PushID(id.c_str());
                    if (ImGui::Selectable(m.at("name").get_ref<const std::string&>().c_str(),
                                          member_ == id)) {
                        member_ = id;
                        if (ui::editor_context)
                            ui::editor_context->selection.select_member(edited, member_);
                    }
#ifdef FORGE_UI_FIXTURE
                    test::observe_item("prefab-source-member:" + id);
#endif
                    ui::help("Choose a source member. Names are display labels; member identity "
                             "survives rename/reparent.");
                    ImGui::PopID();
                }
                std::vector<std::string> siblings;
                std::string parent;
                for (const auto& m : view.at("members"))
                    if (m.at("id") == member_)
                        parent = m.value("parent", "");
                if (!parent.empty()) {
                    for (const auto& m : view.at("members"))
                        if (m.value("parent", "") == parent)
                            siblings.push_back(m.at("id"));
                    const auto found = std::find(siblings.begin(), siblings.end(), member_);
                    const auto index = static_cast<std::size_t>(found - siblings.begin());
                    ImGui::BeginDisabled(index == 0 || index >= siblings.size());
                    if (ui::button("Move member up",
                                   "Reorder this sibling in the source draft. Publish updates "
                                   "instances; scene Undo does not reverse publication."))
                        move_sibling(siblings, index, index - 1, parent, schema);
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(index + 1 >= siblings.size());
                    if (ui::button("Move member down",
                                   "Move the next sibling before this source member."))
                        move_sibling(siblings, index, index + 1, parent, schema);
                    ImGui::EndDisabled();
                    if (variant() && draft_.at("version").get<unsigned>() >= 6 &&
                        draft_.contains("member_order"))
                        for (const auto& order : draft_.at("member_order"))
                            if (order.at("parent") == parent &&
                                ui::button("Revert sibling order",
                                           "Remove this source's order prefix for this parent.")) {
                                source_edit([&] {
                                    set_member_order(PrefabMemberId::parse(parent), {}, schema);
                                });
                                break;
                            }
                }
                ImGui::Separator();
                for (auto& m : view["members"])
                    if (m.at("id") == member_) {
                        const bool inherited = inherited_member(member_);
                        ImGui::TextUnformatted(inherited ? "Inherited member" : "Owned member");
                        char name[1024]{};
                        SDL_strlcpy(name, m.at("name").get<std::string>().c_str(), sizeof(name));
                        ui::property_label_row("Member name");
                        if (ImGui::InputText("##Member name", name, sizeof(name)))
                            rename_member(name);
                        if (inherited) {
                            const auto* intent = selected_override();
                            ImGui::TextUnformatted(intent && intent->contains("name")
                                                       ? "Name overridden"
                                                       : "Name inherited");
                            if (intent && intent->contains("name") &&
                                ui::button("Revert name", "Follow this member's base name again."))
                                revert_name();
                            else if ((!intent || !intent->contains("name")) &&
                                     ui::button("Override name",
                                                "Keep this name as explicit variant intent, even "
                                                "when it equals the base."))
                                rename_member(m.at("name"));
                        }
                        ui::help(
                            "Rename this member in every instance. Its durable member identity is "
                            "unchanged.");
                        draw_member_structure(m, view, schema);
                        draw_nested_prefab(m, scene, project);
                        if (ui::button("+ Add Component",
                                       "Add a registered component to this source member. Publish "
                                       "validates the complete candidate."))
                            ImGui::OpenPopup("Source Add Component");
                        if (ImGui::BeginPopup("Source Add Component")) {
                            ImGui::InputTextWithHint("##source-component-search",
                                                     "Search components...", component_filter_,
                                                     sizeof(component_filter_));
                            ui::help("Search by registered component name or category.");
                            ui::component_choices(
                                schema, m["components"], component_filter_, [&](const Json& type) {
                                    const std::string key = type.at("id");
                                    Json defaults = Json::object();
                                    for (const auto& field : type.at("fields"))
                                        defaults[field.at("id").get<std::string>()] =
                                            field.at("default");
                                    if (type.value("custom", false))
                                        defaults["$forge"] = type.at("admission");
                                    set_component(key, defaults);
                                });
                            ImGui::EndPopup();
                        }
                        ui::help(
                            "Registered component defaults for this prefab member. Dynamic bodies "
                            "need spatial World binding.");
                        if (inherited && draft_.at("version").get<unsigned>() >= 5) {
                            const auto* intent = selected_override();
                            const auto suppressed =
                                intent ? intent->value("suppressed_components", Json::array())
                                       : Json::array();
                            for (const auto& value : suppressed) {
                                const auto key = value.get<std::string>();
                                ui::IdScope suppressed_scope(key.c_str());
                                ImGui::TextWrapped("Suppressed: %s", prefab_component_label(key));
                                if (ui::button("Restore component",
                                               "Remove suppression and inherit the current base "
                                               "component. Save publishes this draft."))
                                    revert_component(key);
                                FORGE_UI_PROBE("prefab-component-restore:" + key);
                            }
                        }
                        for (const auto& component : schema.at("components")) {
                            const std::string key = component.at("id");
                            if (!m["components"].contains(key))
                                continue;
                            ui::IdScope component_scope(key.c_str());
                            ui::heading(
                                component
                                    .value("display_name", std::string(prefab_component_label(key)))
                                    .c_str(),
                                "Edit prefab defaults. Explicit instance overrides are "
                                "preserved at publication.");
                            if (component.value("custom", false) &&
                                (!m["components"][key].is_object() ||
                                 m["components"][key].value("$forge", Json()) !=
                                     component.at("admission"))) {
                                ImGui::TextWrapped(
                                    "Schema changed or unavailable. This component remains "
                                    "preserved and read only until explicitly migrated.");
                                ui::help("Publishing unrelated prefab changes does not reinterpret "
                                         "or discard this component's unknown values.");
                                continue;
                            }
                            const auto* intent = inherited ? selected_override() : nullptr;
                            const bool whole = intent && intent->at("components").contains(key);
                            const auto masks =
                                intent ? intent->value("property_overrides", Json::object())
                                       : Json::object();
                            if (component.value("custom", false) && masks.contains(key) &&
                                (!masks.at(key).is_object() ||
                                 masks.at(key).value("$forge", Json()) !=
                                     component.at("admission"))) {
                                ImGui::TextWrapped(
                                    "Override schema is unavailable. Its intent is preserved and "
                                    "read only until explicitly migrated.");
                                continue;
                            }
                            if (inherited)
                                ImGui::TextUnformatted(whole ? "Component overridden"
                                                       : masks.contains(key)
                                                           ? "Properties overridden"
                                                           : "Component inherited");
                            if (inherited && !whole && !masks.contains(key)) {
                                if (ui::button("Override component",
                                               "Keep this complete component as explicit variant "
                                               "intent, including equal values."))
                                    set_component(key, m.at("components").at(key));
                                FORGE_UI_PROBE("prefab-component-override:" + key);
                                ImGui::SameLine();
                            }
                            if (inherited || component.value("optional", false)) {
                                ImGui::BeginDisabled(inherited && !whole && !masks.contains(key));
                                const bool remove = ui::button(
                                    inherited ? "Revert component" : "Remove component",
                                    "Remove this component from the draft. Publish "
                                    "validates affected instances; Discard restores the "
                                    "draft baseline. For a variant, restore inheritance.");
                                FORGE_UI_PROBE("prefab-component-revert:" + key);
                                ImGui::EndDisabled();
                                if (remove) {
                                    revert_component(key);
                                    continue;
                                }
                            }
                            if (inherited && component.value("optional", false)) {
                                if (ui::button("Suppress component",
                                               "Keep this component absent from the variant, "
                                               "including after base updates. Replaces local "
                                               "component/property overrides; Restore inherits "
                                               "again. First use explicitly enables source5.")) {
                                    suppress_component(key, schema);
                                    continue;
                                }
                                FORGE_UI_PROBE("prefab-component-suppress:" + key);
                            }
                            if (key == "forge.local_rotation") {
                                const auto& q = m["components"][key];
                                const auto angles =
                                    rotation_to_euler({q.value("x", 0.f), q.value("y", 0.f),
                                                       q.value("z", 0.f), q.value("w", 1.f)});
                                float degrees[3] = {float(angles[0]), float(angles[1]),
                                                    float(angles[2])};
                                if (ui::xyz_input("Rotation (degrees)", degrees,
                                                  "Source local rotation: X, Y and Z Euler angles "
                                                  "in degrees.") &&
                                    std::isfinite(degrees[0]) && std::isfinite(degrees[1]) &&
                                    std::isfinite(degrees[2]) && std::abs(degrees[0]) <= 360000 &&
                                    std::abs(degrees[1]) <= 360000 &&
                                    std::abs(degrees[2]) <= 360000) {
                                    const auto rotation =
                                        rotation_from_euler({degrees[0], degrees[1], degrees[2]});
                                    auto value = m["components"][key];
                                    value.update({{"x", rotation.x},
                                                  {"y", rotation.y},
                                                  {"z", rotation.z},
                                                  {"w", rotation.w}});
                                    set_component(key, value);
                                }
                                ui::help("Local Euler angles in degrees, stored as one normalized "
                                         "quaternion channel.");
                                continue;
                            }
                            for (const auto& field : component.at("fields")) {
                                const std::string f = field.at("id");
                                ui::IdScope field_scope(f.c_str());
                                if (key == "forge.sprite_renderer" && f == "frame_regions" &&
                                    atlas_frames) {
                                    const auto owner =
                                        "prefab:" + path_text(project.project()) + "/" +
                                        draft_.at("asset_id").get<std::string>() + "/" + member_;
                                    if (auto patch = atlas_frames(
                                            ui::sprite_frame_context(owner, m["components"][key])))
                                        for (const auto& [name, next] : patch->items())
                                            set_property(key, name, next);
                                }
                                auto value = m["components"][key].value(f, field.at("default"));
                                PrefabPropertyScope prefab_properties(view, &resolved);
                                bool changed =
                                    key == "forge.tile_map_collider_2d" && f == "layer" &&
                                            tile_map_layer
                                        ? tile_map_layer(m["components"], value)
                                        : property_field(project.project(), field, value, false);
                                FORGE_UI_PROBE("prefab-source-field:" + key + ":" + f);
                                if (inherited && !key.starts_with("forge.local_") &&
                                    ImGui::BeginPopupContextItem("Prefab field actions")) {
                                    ImGui::BeginDisabled(field.value("read_only", false));
                                    if (whole) {
                                        if (ImGui::MenuItem("Revert component"))
                                            revert_component(key);
                                    } else if (masks.contains(key) && masks.at(key).contains(f)) {
                                        if (ImGui::MenuItem("Revert field"))
                                            revert_property(key, f);
                                    } else {
                                        if (ImGui::MenuItem("Override field"))
                                            set_property(key, f, value);
                                        FORGE_UI_PROBE("prefab-field-override:" + key + ":" + f);
                                    }
                                    ImGui::EndDisabled();
                                    ImGui::EndPopup();
                                }
                                if (key == "forge.sprite_renderer" && f == "frame" &&
                                    sprite_frames) {
                                    const auto owner =
                                        "prefab:" + path_text(project.project()) + "/" +
                                        draft_.at("asset_id").get<std::string>() + "/" + member_ +
                                        "/" + draft_.at("revision").dump();
                                    ImGui::BeginDisabled(field.value("read_only", false));
                                    changed |= sprite_frame_field(sprite_frames, owner,
                                                                  m["components"][key], value);
                                    ImGui::EndDisabled();
                                }
                                if (changed) {
                                    auto admitted = value;
                                    bool found_reference = false;
                                    detail::transform_prefab_reference_field(
                                        field, admitted,
                                        [&](const Json& reference) {
                                            if (reference.contains("prefab")) {
                                                const auto path = detail::checked_prefab_reference(
                                                    reference, edited, 7);
                                                if (!path.instances.empty())
                                                    draft_ = PrefabDocument(draft_)
                                                                 .with_nested_references()
                                                                 .source;
                                                else if (draft_.at("version").get<unsigned>() < 3)
                                                    draft_["version"] = 3;
                                            }
                                            return reference;
                                        },
                                        found_reference);
                                    set_property(key, f, value);
                                }
                                if (inherited && !whole && masks.contains(key)) {
                                    const bool field_owned =
                                        masks.contains(key) && masks.at(key).contains(f);
                                    ImGui::TextUnformatted(field_owned ? "Overridden"
                                                                       : "Inherited");
                                    if (field_owned &&
                                        ui::button("Revert field",
                                                   "Remove this field's explicit intent and follow "
                                                   "the base value."))
                                        revert_property(key, f);
                                }
                            }
                        }
                        for (const auto& [key, value] : m.at("components").items()) {
                            (void)value;
                            const bool known = std::any_of(
                                schema.at("components").begin(), schema.at("components").end(),
                                [&](const auto& type) { return type.at("id") == key; });
                            if (!known) {
                                ui::IdScope unknown_scope(key.c_str());
                                ui::heading(
                                    (key + " (unavailable)").c_str(),
                                    "Stored prefab component data with no admitted schema.");
                                ImGui::TextWrapped(
                                    "Its values are preserved and read only. Inspect the matching "
                                    "gameplay schema or explicitly migrate before editing.");
                                ui::help(
                                    "Publishing unrelated prefab changes retains this payload. "
                                    "It is never replaced with new defaults.");
                            }
                        }
                    }
                if (ui::button("Add child", "Add a new member under the selected source member. It "
                                            "receives a fresh member identity.")) {
                    source_edit([&] {
                        member_ = add_child(PrefabMemberId::parse(member_), schema).str();
                        if (ui::editor_context)
                            ui::editor_context->selection.select_member(edited, member_);
                    });
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(member_ == draft_.at("root").get_ref<const std::string&>());
                if (ui::button("Remove member subtree",
                               "Remove this effective branch. Variants retain definitions and "
                               "removal intent for restoration; Publish updates instances."))
                    source_edit(
                        [&] { set_member_removed(PrefabMemberId::parse(member_), true, schema); });
                ImGui::EndDisabled();
                if (variant())
                    draw_unavailable_members(view, resolved.at(draft_.at("base").get<AssetId>()),
                                             schema);
                if (ui::button(
                        "Publish source",
                        "Validate and realize the candidate, atomically replace this one prefab "
                        "asset, then activate prepared instances. A failed candidate leaves the "
                        "previous source, instances and history intact.")) {
                    publish(scene, project);
                }
                ImGui::SameLine();
                if (ui::button("Discard edits",
                               "Reload the last published source into this window. "
                               "Unpublished edits are discarded.")) {
                    draft_ = baseline_;
                    member_ = draft_.at("root");
                    error_.clear();
                }
            } catch (const std::exception& e) {
                error_ = e.what();
            }
            ImGui::EndDisabled();
            if (!error_.empty()) {
                ui::field_error(error_);
                ui::report_error("prefab-source", error_);
            }
        }
        ImGui::End();
        if (close_requested_)
            ImGui::OpenPopup("Unsaved prefab source");
        if (ImGui::BeginPopupModal("Unsaved prefab source", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "Publish this prefab before closing? Scene Save does not save this draft.");
            ui::help("Publishing changes the shared asset; Discard affects only this unpublished "
                     "draft. Cancel keeps editing.");
            ImGui::BeginDisabled(locked);
            bool finish = false;
            if (ui::button("Publish",
                           "Validate and publish before closing. Failure keeps the draft open."))
                finish = resolve_close(ui::DraftResolution::Save, scene, project);
            if (ui::button(
                    "Discard",
                    "Discard unpublished source changes; committed assets remain unchanged."))
                finish = resolve_close(ui::DraftResolution::Discard, scene, project);
            ImGui::EndDisabled();
            if (ui::button("Cancel", "Keep the draft and cancel the pending close or switch.")) {
                resolve_close(ui::DraftResolution::Cancel, scene, project);
                ImGui::CloseCurrentPopup();
            }
            if (!error_.empty())
                ui::field_error(error_);
            if (finish) {
                close_requested_ = false;
                open_ = false;
                ImGui::CloseCurrentPopup();
                if (pending_asset_) {
                    auto next = pending_asset_;
                    pending_asset_ = {};
                    edit_source(scene, project, next);
                }
            }
            ImGui::EndPopup();
        }
    }

  private:
    void draw_nested_prefab(const Json& member, Scene& scene, SceneDocument& project) {
        const auto mount = member.at("id").get<PrefabMemberId>();
        const auto child = detail::nested_prefab_asset(member, 7);
        std::string preview = "None";
        if (child) {
            const auto at = project.prefabs().records().find(*child);
            preview = at == project.prefabs().records().end() ? child->str()
                                                              : path_utf8(at->second.source);
        }
        ui::property_label_row("Nested Prefab");
        if (ImGui::BeginCombo("##Nested Prefab", preview.c_str())) {
            if (ImGui::Selectable("None", !child))
                source_edit([&] { set_nested_prefab(scene, project, mount, {}); });
            FORGE_UI_PROBE("prefab-nested-none");
            const auto own = draft_.at("asset_id").get<AssetId>();
            for (const auto& [asset, record] : project.prefabs().records()) {
                if (asset == own)
                    continue;
                ui::IdScope scope(asset.str().c_str());
                if (ImGui::Selectable(path_utf8(record.source).c_str(), child && *child == asset))
                    source_edit([&] { set_nested_prefab(scene, project, mount, asset); });
                FORGE_UI_PROBE("prefab-nested-asset:" + asset.str());
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("prefab-nested-choice");
        ui::help("Create a linked child instance below this member in every instance. "
                 "This member controls its mount transform. Publish saves the link; "
                 "child values remain owned by their own source.");
        const auto* intent = selected_override();
        if (inherited_member(mount.str()) && draft_.at("version").get<unsigned>() >= 7 && intent &&
            intent->contains("prefab_instance")) {
            if (ui::button("Use inherited nested Prefab",
                           "Remove this link override and follow the current base link."))
                source_edit([&] { revert_nested_prefab(scene, project, mount); });
            FORGE_UI_PROBE("prefab-nested-revert");
        }
    }
    template <class Edit> void source_edit(Edit edit) {
        try {
            edit();
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void move_sibling(std::vector<std::string> siblings, std::size_t from, std::size_t to,
                      const std::string& parent, const Json& schema) {
        std::swap(siblings.at(from), siblings.at(to));
        std::vector<PrefabMemberId> prefix;
        std::size_t next = 0;
        const std::set<std::string> active(siblings.begin(), siblings.end());
        if (variant() && draft_.at("version").get<unsigned>() >= 6 &&
            draft_.contains("member_order"))
            for (const auto& order : draft_.at("member_order"))
                if (order.at("parent") == parent)
                    for (const auto& id : order.at("members"))
                        prefix.push_back(PrefabMemberId::parse(
                            active.contains(id.get<std::string>()) ? siblings.at(next++)
                                                                   : id.get<std::string>()));
        while (next < siblings.size())
            prefix.push_back(PrefabMemberId::parse(siblings.at(next++)));
        source_edit([&] { set_member_order(PrefabMemberId::parse(parent), prefix, schema); });
    }
    void draw_member_structure(const Json& member, const Json& view, const Json& schema) {
        const auto id = member.at("id").get<PrefabMemberId>();
        if (member.at("id") == draft_.at("root"))
            return;
        const auto identity = id.str();
        const auto parent = member.value("parent", std::string{});
        ui::property_label_row("Parent member");
        if (ImGui::BeginCombo("##Parent member", prefab_member_label(view, parent).c_str())) {
            for (const auto& target : view.at("members"))
                if (target.at("id") != Json(id)) {
                    ui::IdScope target_scope(target.at("id").get_ref<const std::string&>().c_str());
                    if (ImGui::Selectable(target.at("name").get_ref<const std::string&>().c_str(),
                                          target.at("id") == parent))
                        source_edit([&] {
                            set_member_parent(id, target.at("id").get<PrefabMemberId>(), schema);
                        });
                    FORGE_UI_PROBE("prefab-parent-target:" + target.at("id").get<std::string>());
                }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("prefab-source-parent:" + identity);
        ui::help("Change this member's structural parent, retaining local channels. Cycles reject "
                 "the edit. A missing parent leaves the branch unavailable until repaired.");
        auto intent = member_override(identity);
        if (inherited_member(identity) && draft_.at("version").get<unsigned>() >= 6 && intent &&
            intent->contains("parent") &&
            ui::button("Revert parent", "Follow the current base parent again."))
            source_edit([&] { revert_member_structure(id, "parent", schema); });
        auto binding = member.value("spatial", Json{{"mode", "follow_structure"}});
        ui::property_label_row("Spatial binding");
        const auto label = [](const std::string& mode) {
            return mode == "world"      ? "World"
                   : mode == "explicit" ? "Explicit attachment"
                                        : "Follow parent";
        };
        if (ImGui::BeginCombo("##Member binding", label(binding.at("mode")))) {
            for (const char* mode : {"follow_structure", "world", "explicit"}) {
                if (ImGui::Selectable(label(mode), binding.at("mode") == mode))
                    source_edit([&] {
                        Json next{{"mode", mode}};
                        if (std::string_view(mode) == "explicit")
                            next["member"] = draft_.at("root");
                        set_member_spatial(id, next, schema);
                    });
                FORGE_UI_PROBE("prefab-spatial-mode:" + std::string(mode));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("prefab-source-spatial:" + identity);
        ui::help(
            "Follow the structural parent, use world coordinates, or attach to a stable source "
            "member. Local channels are retained; cyclic attachments reject the edit.");
        if (binding.at("mode") == "explicit") {
            ui::property_label_row("Spatial target");
            if (ImGui::BeginCombo("##Spatial target",
                                  prefab_member_label(view, binding.at("member")).c_str())) {
                for (const auto& target : view.at("members"))
                    if (target.at("id") != Json(id)) {
                        ui::IdScope scope(target.at("id").get_ref<const std::string&>().c_str());
                        if (ImGui::Selectable(
                                target.at("name").get_ref<const std::string&>().c_str()))
                            source_edit([&] {
                                auto next = binding;
                                next["member"] = target.at("id");
                                set_member_spatial(id, next, schema);
                            });
                        FORGE_UI_PROBE("prefab-spatial-target:" +
                                       target.at("id").get<std::string>());
                    }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("prefab-source-spatial-target:" + identity);
        }
        intent = member_override(identity);
        if (inherited_member(identity) && draft_.at("version").get<unsigned>() >= 6 && intent &&
            intent->contains("spatial") &&
            ui::button("Revert spatial binding", "Follow the current base spatial binding again."))
            source_edit([&] { revert_member_structure(id, "spatial", schema); });
    }
    void draw_unavailable_members(const Json& view, const Json& base, const Json& schema) {
        std::map<std::string, Json> candidates;
        const auto remember = [&](const Json& row) {
            auto& member = candidates[row.at("id").get<std::string>()];
            member = {{"id", row.at("id")}, {"name", row.at("name")}};
            for (const char* field : {"parent", "spatial"})
                if (row.contains(field))
                    member[field] = row.at(field);
        };
        for (const auto& member : base.at("members"))
            remember(member);
        for (const auto* collection : PrefabDocument::authored_member_arrays(draft_))
            for (const auto& row : draft_.at(collection)) {
                const auto id = row.at("id").get<std::string>();
                if (std::string_view(collection) != "overrides")
                    remember(row);
                else {
                    auto& member = candidates[id];
                    if (member.is_null())
                        member = {{"id", id}, {"name", "Missing inherited member"}};
                    for (const char* field : {"name", "parent", "spatial"})
                        if (row.contains(field) && (std::string_view(field) == "name" ||
                                                    draft_.at("version").get<unsigned>() >= 6))
                            member[field] = row.at(field);
                }
            }
        for (const auto& member : view.at("members"))
            candidates.erase(member.at("id").get<std::string>());
        if (candidates.empty())
            return;
        ui::heading("Unavailable members",
                    "Removed members and branches with missing parents or spatial targets.");
        ImGui::TextWrapped("Restore clears only this source's removal. Repair attachments here; "
                           "members missing from the base return when that base restores them.");
        for (const auto& [id, member] : candidates) {
            ui::IdScope scope(id.c_str());
            const bool expanded = ImGui::TreeNode("##Unavailable member", "%s",
                                                  member.at("name").get<std::string>().c_str());
            FORGE_UI_PROBE("prefab-unavailable:" + id);
            if (!expanded)
                continue;
            ImGui::TextWrapped("Member: %s | %s", id.c_str(),
                               inherited_member(id) ? "Inherited" : "Owned addition");
            const auto* intent = detail::owned_prefab_member(draft_, id);
            if (!intent)
                intent = member_override(id);
            const bool removed = draft_.at("version").get<unsigned>() >= 6 && intent &&
                                 intent->value("removed", false);
            if (removed) {
                if (ui::button("Restore member", "Clear this source's retained removal flag."))
                    source_edit(
                        [&] { set_member_removed(PrefabMemberId::parse(id), false, schema); });
                FORGE_UI_PROBE("prefab-member-restore:" + id);
            }
            draw_member_structure(member, view, schema);
            ImGui::TreePop();
        }
    }
    const Json* member_override(const std::string& id) const {
        if (variant())
            for (const auto& row : draft_.at("overrides"))
                if (row.at("id") == id)
                    return &row;
        return nullptr;
    }
    const Json* selected_override() const { return member_override(member_); }
    Json& selected_intent() { return detail::prefab_member_intent(draft_, member_); }
    void prune_intent() { detail::prune_prefab_member_intent(draft_, member_); }
    void finish_close() {
        open_ = false;
        if (ui::editor_context &&
            ui::editor_context->selection.kind() == ui::SelectionKind::PrefabMember)
            ui::editor_context->selection.select_asset(ui::editor_context->selection.asset());
    }
    PrefabApplyCandidate apply_candidate_;
    std::uint64_t apply_revision_ = 0, apply_generation_ = 0;
    std::uint64_t source_session_ = 0;
    std::string apply_error_;
    char component_filter_[192]{};
    bool close_requested_ = false, save_requested_ = false, focus_requested_ = false;
    AssetId pending_asset_;
    AssetId selected_;
    Json baseline_, draft_;
    PrefabSources baseline_bases_;
    std::string member_, error_;
    std::filesystem::path project_, content_project_;
    bool open_ = false;
    char path_[1024] = "Assets/New.prefab.json";
};
} // namespace forge
