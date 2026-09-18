#pragma once
#include "audio_inspector.hpp"
#include "document.hpp"
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
    void edit_source(SceneDocument& project, AssetId asset) {
        auto source = project.prefabs().source(asset);
        baseline_ = source;
        draft_ = std::move(source);
        member_ = draft_.at("root");
        selected_ = asset;
        project_ = project.project();
        content_project_ = project_;
        open_ = true;
        error_.clear();
    }
    void content(Scene& scene, SceneDocument& project, std::string& selection, bool locked) {
        if (content_project_ != project.project()) {
            selected_ = {};
            content_project_ = project.project();
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
            });
        ImGui::EndDisabled();
        if (ui::button(
                "Refresh prefabs",
                "Validate external source edits and reconcile instances. Failed candidates keep "
                "the previous revision. Successful source changes clear scene history."))
            run([&] { project.prefabs().refresh(scene); });
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
    }
    void draw(Scene& scene, SceneDocument& project, bool locked) {
        if (project_ != project.project())
            open_ = false;
        if (!open_)
            return;
        ImGui::SetNextWindowSize({620 * ui::interface_scale, 620 * ui::interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Prefab source", &open_)) {
            ImGui::End();
            return;
        }
        const auto edited = baseline_.at("asset_id").get<AssetId>();
        if (project.prefabs().records().contains(edited))
            ImGui::TextWrapped("Source: %s",
                               path_text(project.prefabs().records().at(edited).source).c_str());
        ui::help("The file being edited in this source window.");
        ImGui::TextWrapped(
            "Edit the reusable source here. Publishing updates non-overridden instance values and "
            "clears scene Undo/Redo. Scene Undo never reverses a published asset edit.");
        ui::help("Closing this window discards unpublished edits.");
        ImGui::BeginDisabled(locked);
        try {
            for (const auto& m : draft_.at("members")) {
                const std::string id = m.at("id");
                ImGui::PushID(id.c_str());
                if (ImGui::Selectable(m.at("name").get_ref<const std::string&>().c_str(),
                                      member_ == id))
                    member_ = id;
                ui::help("Choose a source member. Names are display labels; member identity "
                         "survives rename/reparent.");
                ImGui::PopID();
            }
            ImGui::Separator();
            for (auto& m : draft_["members"])
                if (m.at("id") == member_) {
                    char name[1024]{};
                    SDL_strlcpy(name, m.at("name").get<std::string>().c_str(), sizeof(name));
                    if (ImGui::InputText("Member name", name, sizeof(name)))
                        m["name"] = name;
                    ui::help("Rename this member in every instance. Its durable member identity is "
                             "unchanged.");
                    if (member_ != draft_.at("root").get_ref<const std::string&>()) {
                        if (ImGui::BeginCombo(
                                "Parent member",
                                prefab_member_label(draft_, m.at("parent")).c_str())) {
                            for (const auto& target : draft_.at("members"))
                                if (target.at("id") != member_) {
                                    ImGui::PushID(
                                        target.at("id").get_ref<const std::string&>().c_str());
                                    if (ImGui::Selectable(
                                            target.at("name").get_ref<const std::string&>().c_str(),
                                            m.at("parent") == target.at("id")))
                                        m["parent"] = target.at("id");
                                    ui::help("Change the structural parent, keeping local "
                                             "channels. Cycles are rejected at publication.");
                                    ImGui::PopID();
                                }
                            ImGui::EndCombo();
                        }
                        ui::help("Source hierarchy uses validated structured parenting. Instances "
                                 "cannot rearrange its interiors.");
                    }
                    if (member_ != draft_.at("root").get_ref<const std::string&>()) {
                        auto binding = m.value("spatial", Json{{"mode", "follow_structure"}});
                        if (ImGui::BeginCombo(
                                "Member space",
                                binding.at("mode").get_ref<const std::string&>().c_str())) {
                            for (const char* mode : {"follow_structure", "world", "explicit"}) {
                                if (ImGui::Selectable(mode, binding.at("mode") == mode)) {
                                    binding = {{"mode", mode}};
                                    if (std::string(mode) == "explicit")
                                        binding["member"] = draft_.at("root");
                                    m["spatial"] = binding;
                                }
                                ui::help("Follow the structural parent, use world coordinates, or "
                                         "attach to another source member. Local channels are "
                                         "retained.");
                            }
                            ImGui::EndCombo();
                        }
                        ui::help("Source member spatial attachment. Instances inherit this "
                                 "source-owned binding.");
                        if (binding.at("mode") == "explicit") {
                            if (ImGui::BeginCombo(
                                    "Spatial target",
                                    prefab_member_label(draft_, binding.at("member")).c_str())) {
                                for (const auto& target : draft_.at("members"))
                                    if (target.at("id") != member_) {
                                        ImGui::PushID(
                                            target.at("id").get_ref<const std::string&>().c_str());
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
                    if (ImGui::BeginCombo("Add optional component", "Choose component")) {
                        for (const auto& component : schema.at("components")) {
                            const std::string key = component.at("id");
                            if (!component.value("optional", false) ||
                                m["components"].contains(key))
                                continue;
                            if (ImGui::Selectable(key.c_str()))
                                for (const auto& field : component.at("fields"))
                                    m["components"][key][field.at("id").get<std::string>()] =
                                        field.at("default");
                            ui::help(
                                "Add optional source defaults. Publish validates this prefab "
                                "candidate; runtime validates collider realization before Play.");
                        }
                        ImGui::EndCombo();
                    }
                    ui::help("Body and collider defaults for this prefab member. Dynamic bodies "
                             "need spatial World binding.");
                    for (const auto& component : schema.at("components")) {
                        const std::string key = component.at("id");
                        if (!m["components"].contains(key))
                            continue;
                        ImGui::PushID(key.c_str());
                        ui::heading(prefab_component_label(key),
                                    "Edit prefab defaults. Explicit instance overrides are "
                                    "preserved at publication.");
                        if (key == "forge.local_rotation") {
                            const auto& q = m["components"][key];
                            const auto angles =
                                rotation_to_euler({q.at("x"), q.at("y"), q.at("z"), q.at("w")});
                            float degrees[3] = {float(angles[0]), float(angles[1]),
                                                float(angles[2])};
                            if (ImGui::InputFloat3("Degrees XYZ", degrees) &&
                                std::isfinite(degrees[0]) && std::isfinite(degrees[1]) &&
                                std::isfinite(degrees[2]) && std::abs(degrees[0]) <= 360000 &&
                                std::abs(degrees[1]) <= 360000 && std::abs(degrees[2]) <= 360000) {
                                const auto rotation =
                                    rotation_from_euler({degrees[0], degrees[1], degrees[2]});
                                m["components"][key].update({{"x", rotation.x},
                                                             {"y", rotation.y},
                                                             {"z", rotation.z},
                                                             {"w", rotation.w}});
                            }
                            ui::help("Local Euler angles in degrees, stored as one normalized "
                                     "quaternion channel.");
                            ImGui::PopID();
                            continue;
                        }
                        for (const auto& field : component.at("fields")) {
                            const std::string f = field.at("id");
                            if (key.starts_with("forge.audio_") ||
                                key.starts_with("forge.navigation_") || key == "forge.animator") {
                                (void)audio_field(project.project(), field,
                                                  m["components"][key][f]);
                                continue;
                            }
                            double n = m["components"][key].at(f).get<double>();
                            if (ImGui::InputDouble(f.c_str(), &n, 0, 0, "%.4f")) {
                                if (field.at("type") == "uint32") {
                                    if (std::isfinite(n) && n >= 0 &&
                                        n <= field.value("maximum", 3.0) && std::floor(n) == n)
                                        m["components"][key][f] = static_cast<unsigned>(n);
                                } else
                                    m["components"][key][f] = n;
                            }
                            ui::help(key == "forge.local_rotation"
                                         ? "Quaternion component. Publication requires a finite "
                                           "normalized quaternion; use a complete valid rotation."
                                         : "Source property. Publication validates the supported "
                                           "range before changing instances.");
                        }
                        ImGui::PopID();
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
            if (ui::button("Publish source",
                           "Validate and realize the candidate, atomically replace this one prefab "
                           "asset, then activate prepared instances. A failed candidate leaves the "
                           "previous source, instances and history intact.")) {
                project.check_ownership();
                project.prefabs().publish(scene, baseline_, draft_);
                baseline_ = project.prefabs().source(baseline_.at("asset_id").get<AssetId>());
                draft_ = baseline_;
                error_.clear();
            }
            ImGui::SameLine();
            if (ui::button("Discard edits", "Reload the last published source into this window. "
                                            "Unpublished edits are discarded.")) {
                draft_ = baseline_;
                member_ = draft_.at("root");
                error_.clear();
            }
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        ImGui::EndDisabled();
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::End();
    }

  private:
    AssetId selected_;
    Json baseline_, draft_;
    std::string member_, error_;
    std::filesystem::path project_, content_project_;
    bool open_ = false;
    char path_[1024] = "Assets/New.prefab.json";
};
} // namespace forge
