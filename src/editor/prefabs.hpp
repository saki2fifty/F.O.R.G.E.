#pragma once
#include "component_choices.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include "widgets.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <set>
namespace forge {
inline const char* prefab_component_label(const std::string& key) {
    if (key == "forge.navigation_agent")
        return "Navigation Agent";
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
    if (key == "forge.physics_body")
        return "Physics Body";
    if (key == "forge.box_collider")
        return "Box Collider";
    if (key == "forge.sphere_collider")
        return "Sphere Collider";
    if (key == "forge.capsule_collider")
        return "Capsule Collider";
    return key.c_str();
}
inline std::string prefab_member_label(const Json& document, const std::string& id) {
    for (const auto& member : document.at("members"))
        if (member.at("id") == id)
            return member.at("name");
    return "Missing member";
}
class PrefabEditor {
  public:
    bool dirty() const { return open_ && draft_ != baseline_; }
    bool is_open() const { return open_; }
    bool close_cancelled = false;
    void request_close() {
        if (dirty())
            close_requested_ = true;
        else
            finish_close();
    }
    void request_save() { save_requested_ = true; }
    void rename_member(const std::string& name) {
        for (auto& member : draft_["members"])
            if (member.at("id") == member_)
                member["name"] = name;
    }
    void set_property(const std::string& component, const std::string& field, const Json& value) {
        for (auto& member : draft_["members"])
            if (member.at("id") == member_)
                member.at("components").at(component).at(field) = value;
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
            project.prefabs().publish(scene, baseline_, draft_);
            baseline_ = project.prefabs().source(baseline_.at("asset_id").get<AssetId>());
            draft_ = baseline_;
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void edit_source(SceneDocument& project, AssetId asset) {
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
        baseline_ = source;
        draft_ = std::move(source);
        member_ = draft_.at("root");
        selected_ = asset;
        project_ = project.project();
        content_project_ = project_;
        open_ = true;
        focus_requested_ = true;
        error_.clear();
        if (ui::editor_context)
            ui::editor_context->selection.select_member(asset, member_);
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
            run([&] { selection = instantiate_prefab(scene, selected_); });
        ImGui::SameLine();
        if (ui::button("Edit source", "Open the reusable prefab definition. Published edits affect "
                                      "all instances without overriding their explicit edits."))
            run([&] { edit_source(project, selected_); });
        if (ui::button("Duplicate asset", "Create an independent prefab at the destination path "
                                          "with new asset and member identities."))
            run([&] {
                selected_ =
                    project.prefabs().duplicate(scene, selected_, std::filesystem::u8path(path_));
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
                edit_source(project, p.at("asset").get<AssetId>());
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
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
                for (const auto& m : draft_.at("members")) {
                    const std::string id = m.at("id");
                    ImGui::PushID(id.c_str());
                    if (ImGui::Selectable(m.at("name").get_ref<const std::string&>().c_str(),
                                          member_ == id)) {
                        member_ = id;
                        if (ui::editor_context)
                            ui::editor_context->selection.select_member(edited, member_);
                    }
                    ui::help("Choose a source member. Names are display labels; member identity "
                             "survives rename/reparent.");
                    ImGui::PopID();
                }
                std::vector<std::string> siblings;
                std::string parent;
                for (const auto& m : draft_.at("members"))
                    if (m.at("id") == member_)
                        parent = m.value("parent", "");
                if (!parent.empty()) {
                    for (const auto& m : draft_.at("members"))
                        if (m.value("parent", "") == parent)
                            siblings.push_back(m.at("id"));
                    const auto found = std::find(siblings.begin(), siblings.end(), member_);
                    const auto index = static_cast<std::size_t>(found - siblings.begin());
                    ImGui::BeginDisabled(index == 0 || index >= siblings.size());
                    if (ui::button("Move member up",
                                   "Reorder this sibling in the source draft. Publish updates "
                                   "instances; scene Undo does not reverse publication."))
                        draft_ = PrefabDocument(draft_)
                                     .reorder_member(PrefabMemberId::parse(member_),
                                                     PrefabMemberId::parse(siblings.at(index - 1)))
                                     .source;
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(index + 1 >= siblings.size());
                    if (ui::button("Move member down",
                                   "Move the next sibling before this source member."))
                        draft_ = PrefabDocument(draft_)
                                     .reorder_member(PrefabMemberId::parse(siblings.at(index + 1)),
                                                     PrefabMemberId::parse(member_))
                                     .source;
                    ImGui::EndDisabled();
                }
                ImGui::Separator();
                for (auto& m : draft_["members"])
                    if (m.at("id") == member_) {
                        char name[1024]{};
                        SDL_strlcpy(name, m.at("name").get<std::string>().c_str(), sizeof(name));
                        ui::property_label_row("Member name");
                        if (ImGui::InputText("##Member name", name, sizeof(name)))
                            rename_member(name);
                        ui::help(
                            "Rename this member in every instance. Its durable member identity is "
                            "unchanged.");
                        if (member_ != draft_.at("root").get_ref<const std::string&>()) {
                            ui::property_label_row("Parent member");
                            if (ImGui::BeginCombo(
                                    "##Parent member",
                                    prefab_member_label(draft_, m.at("parent")).c_str())) {
                                for (const auto& target : draft_.at("members"))
                                    if (target.at("id") != member_) {
                                        ImGui::PushID(
                                            target.at("id").get_ref<const std::string&>().c_str());
                                        if (ImGui::Selectable(target.at("name")
                                                                  .get_ref<const std::string&>()
                                                                  .c_str(),
                                                              m.at("parent") == target.at("id")))
                                            m["parent"] = target.at("id");
                                        ui::help("Change the structural parent, keeping local "
                                                 "channels. Cycles are rejected at publication.");
                                        ImGui::PopID();
                                    }
                                ImGui::EndCombo();
                            }
                            ui::help(
                                "Source hierarchy uses validated structured parenting. Instances "
                                "cannot rearrange its interiors.");
                        }
                        if (member_ != draft_.at("root").get_ref<const std::string&>()) {
                            auto binding = m.value("spatial", Json{{"mode", "follow_structure"}});
                            ui::property_label_row("Spatial binding");
                            const std::string binding_mode = binding.at("mode");
                            if (ImGui::BeginCombo("##Member binding", binding_mode == "world"
                                                                          ? "World"
                                                                      : binding_mode == "explicit"
                                                                          ? "Explicit attachment"
                                                                          : "Follow parent")) {
                                for (const char* mode : {"follow_structure", "world", "explicit"}) {
                                    if (ImGui::Selectable(std::string(mode) == "world" ? "World"
                                                          : std::string(mode) == "explicit"
                                                              ? "Explicit attachment"
                                                              : "Follow parent",
                                                          binding.at("mode") == mode)) {
                                        binding = {{"mode", mode}};
                                        if (std::string(mode) == "explicit")
                                            binding["member"] = draft_.at("root");
                                        m["spatial"] = binding;
                                    }
                                    ui::help(
                                        "Follow the structural parent, use world coordinates, or "
                                        "attach to another source member. Local channels are "
                                        "retained.");
                                }
                                ImGui::EndCombo();
                            }
                            ui::help("Source member spatial attachment. Instances inherit this "
                                     "source-owned binding.");
                            if (binding.at("mode") == "explicit") {
                                ui::property_label_row("Spatial target");
                                if (ImGui::BeginCombo(
                                        "##Spatial target",
                                        prefab_member_label(draft_, binding.at("member"))
                                            .c_str())) {
                                    for (const auto& target : draft_.at("members"))
                                        if (target.at("id") != member_) {
                                            ImGui::PushID(target.at("id")
                                                              .get_ref<const std::string&>()
                                                              .c_str());
                                            if (ImGui::Selectable(target.at("name")
                                                                      .get_ref<const std::string&>()
                                                                      .c_str()))
                                                m["spatial"]["member"] = target.at("id");
                                            ui::help("Persistent source member target; cyclic "
                                                     "attachment is rejected when publishing.");
                                            ImGui::PopID();
                                        }
                                    ImGui::EndCombo();
                                }
                                ui::help("Resolve this target separately through each instance's "
                                         "stable member mapping.");
                            }
                        }
                        const auto schema = scene.schema();
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
                                    for (const auto& field : type.at("fields"))
                                        m["components"][key][field.at("id").get<std::string>()] =
                                            field.at("default");
                                });
                            ImGui::EndPopup();
                        }
                        ui::help(
                            "Registered component defaults for this prefab member. Dynamic bodies "
                            "need spatial World binding.");
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
                            if (component.value("optional", false)) {
                                if (ui::button("Remove component",
                                               "Remove this component from the draft. Publish "
                                               "validates affected instances; Discard restores the "
                                               "draft baseline.")) {
                                    m["components"].erase(key);
                                    continue;
                                }
                            }
                            if (key == "forge.local_rotation") {
                                const auto& q = m["components"][key];
                                const auto angles =
                                    rotation_to_euler({q.at("x"), q.at("y"), q.at("z"), q.at("w")});
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
                                    m["components"][key].update({{"x", rotation.x},
                                                                 {"y", rotation.y},
                                                                 {"z", rotation.z},
                                                                 {"w", rotation.w}});
                                }
                                ui::help("Local Euler angles in degrees, stored as one normalized "
                                         "quaternion channel.");
                                continue;
                            }
                            for (const auto& field : component.at("fields")) {
                                const std::string f = field.at("id");
                                auto value = m["components"][key][f];
                                if (property_field(project.project(), field, value, false))
                                    set_property(key, f, value);
                            }
                        }
                    }
                if (ui::button("Add child", "Add a new member under the selected source member. It "
                                            "receives a fresh member identity.")) {
                    const auto id = PrefabMemberId::generate().str();
                    Json components = Json::object();
                    const auto schema = scene.schema();
                    for (const auto& type : schema.at("components"))
                        if (!type.value("optional", false))
                            for (const auto& field : type.at("fields"))
                                components[type.at("id").get<std::string>()]
                                          [field.at("id").get<std::string>()] = field.at("default");
                    components["forge.local_translation"]["y"] = 1;
                    draft_["members"].push_back({{"id", id},
                                                 {"name", "Child"},
                                                 {"parent", member_},
                                                 {"components", components}});
                    member_ = id;
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(member_ == draft_.at("root").get_ref<const std::string&>());
                if (ui::button("Remove member subtree",
                               "Remove this source subtree. Published instances retain removed "
                               "members' IDs and override data as diagnostics.")) {
                    std::set<std::string> removed{member_};
                    bool changed;
                    do {
                        changed = false;
                        for (const auto& m : draft_["members"])
                            if (removed.contains(m.value("parent", "")))
                                changed |= removed.insert(m.at("id")).second;
                    } while (changed);
                    auto kept = Json::array();
                    for (const auto& m : draft_["members"])
                        if (!removed.contains(m.at("id")))
                            kept.push_back(m);
                    draft_["members"] = kept;
                    member_ = draft_.at("root");
                }
                ImGui::EndDisabled();
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
                    edit_source(project, next);
                }
            }
            ImGui::EndPopup();
        }
    }

  private:
    void finish_close() {
        open_ = false;
        if (ui::editor_context &&
            ui::editor_context->selection.kind() == ui::SelectionKind::PrefabMember)
            ui::editor_context->selection.select_asset(ui::editor_context->selection.asset());
    }
    char component_filter_[192]{};
    bool close_requested_ = false, save_requested_ = false, focus_requested_ = false;
    AssetId pending_asset_;
    AssetId selected_;
    Json baseline_, draft_;
    std::string member_, error_;
    std::filesystem::path project_, content_project_;
    bool open_ = false;
    char path_[1024] = "Assets/New.prefab.json";
};
} // namespace forge
