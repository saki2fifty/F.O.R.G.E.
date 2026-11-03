#pragma once
#include "../mesh_scatter_authoring.hpp"
#include "editor_state.hpp"
#include "scene_cache.hpp"
#include "widgets.hpp"
#include <array>
#include <forge/authoring.hpp>
namespace forge::ui {
class FoliagePlacement {
  public:
    bool open = false;
    void draw(Scene& scene, EditorSelection& selection, bool locked) {
        if (!open)
            return;
        draft_window_size({520 * interface_scale, 760 * interface_scale});
        if (ImGui::Begin("Foliage placement", &open)) {
            if (editor_context)
                editor_context->task.focus(DocumentTask::Scene);
            const auto& view = snapshot_.effective(scene);
            const auto asset = view.at("asset_id").get<AssetId>();
            if (owner_ != &scene || asset != scene_) {
                owner_ = &scene;
                scene_ = asset;
                source_.clear();
                status_.clear();
                error_.clear();
            }
            ImGui::TextWrapped(
                "Place independent copies of one Mesh Renderer in a horizontal "
                "disk. Select a template object, then choose Use selected Mesh "
                "Renderer. Its local rotation, scale and renderer values are copied.");
            if (locked)
                ImGui::TextWrapped("Stop Play and finish active tasks before placing foliage.");
            ImGui::BeginDisabled(locked);
            const auto* selected = find_mesh(view, selection.entity());
            ImGui::BeginDisabled(!selected);
            if (button("Use selected Mesh Renderer", "Set the template and start the disk at its "
                                                     "world position. Other components and "
                                                     "children are not copied.")) {
                source_ = selected->at("id");
                if (selected->value("spatial_resolved", false) &&
                    selected->contains("world_affine")) {
                    const auto& m = selected->at("world_affine");
                    settings_.center = {m.at(3), m.at(7), m.at(11)};
                }
                status_.clear();
                error_.clear();
            }
            FORGE_UI_PROBE("foliage:source");
            ImGui::EndDisabled();
            const auto* source = find_mesh(view, source_);
            ImGui::TextWrapped("Template: %s", source
                                                   ? source->at("name").get<std::string>().c_str()
                                                   : "Choose an available Mesh Renderer");
            ImGui::TextUnformatted("Group name");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##group", name_.data(), name_.size());
            FORGE_UI_PROBE("foliage:name");
            help("Name of the new group. Each placement is an ordinary child in the Hierarchy.");
            auto field = [&](const char* label, const char* key, double& value, const char* tip) {
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-1);
                ImGui::PushID(key);
                if (ImGui::InputDouble("##value", &value, 0, 0, "%.6g"))
                    status_.clear();
                FORGE_UI_PROBE(std::string("foliage:") + key);
                help(tip);
                ImGui::PopID();
            };
            field("Center X (m)", "x", settings_.center[0],
                  "World X coordinate of the disk center.");
            field("Center Y (m)", "y", settings_.center[1],
                  "Every new object origin uses this Y. "
                  "No terrain or surface projection.");
            field("Center Z (m)", "z", settings_.center[2],
                  "World Z coordinate of the disk center.");
            field("Radius (m)", "radius", settings_.radius,
                  "Horizontal disk radius, 0.01 to 1000 m.");
            field("Minimum spacing (m)", "spacing", settings_.spacing,
                  "0 to 2000 m between new origins in this group. Mesh bounds, old objects and "
                  "colliders are not tested. Dense requests can fail without creating objects.");
            ImGui::TextUnformatted("Count");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputInt("##count", &count_, 0, 0))
                status_.clear();
            FORGE_UI_PROBE("foliage:count");
            help("1 to 512 objects per placement. The whole Scene remains limited to 10000 "
                 "entities.");
            ImGui::TextUnformatted("Seed");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputScalar("##seed", ImGuiDataType_U32, &settings_.seed))
                status_.clear();
            FORGE_UI_PROBE("foliage:seed");
            help("0 to 4294967295. Reusing the same settings gives the same local layout; each "
                 "placement receives independent object identities.");
            field("Minimum scale multiplier", "scale_min", settings_.scale_min,
                  "0.01 to 100 times the template's local scale, including its axis signs.");
            field("Maximum scale multiplier", "scale_max", settings_.scale_max,
                  "At least the minimum and at most 100. Final local scale must remain valid.");
            field("Minimum yaw (deg)", "yaw_min", settings_.yaw_min,
                  "-360 to 360 degrees about world +Y, before the template's local rotation.");
            field("Maximum yaw (deg)", "yaw_max", settings_.yaw_max,
                  "At least the minimum and at most 360 degrees.");
            settings_.count = count_ > 0 ? std::uint32_t(count_) : 0;
            ImGui::BeginDisabled(!source);
            auto perform = [&](bool place) {
                try {
                    const auto arguments =
                        detail::mesh_scatter_arguments(source_, settings_, name_.data());
                    if (place) {
                        const auto result = authoring_command(scene, "mesh.scatter", arguments);
                        selection.select_entity(result.at("selected"));
                        status_ =
                            "Foliage placed. Scene Undo removes the entire group. Save the Scene.";
                    } else {
                        // The same complete detached command admission validates renderer,
                        // transforms, layout, names and the Scene budget without publication.
                        preview_authoring(scene, Json::array({{{"operation", "mesh.scatter"},
                                                               {"arguments", arguments}}}));
                        status_ = "Layout fits. No Scene objects have been changed.";
                    }
                    error_.clear();
                    if (editor_context)
                        editor_context->problems.resolve("scene.foliage");
                } catch (const std::exception& e) {
                    error_ = e.what();
                    status_.clear();
                    report_error("scene.foliage", error_);
                }
            };
            if (button("Check layout",
                       "Validate the complete placement without changing the Scene."))
                perform(false);
            FORGE_UI_PROBE("foliage:check");
            if (button("Place foliage", "Create one independent group. Scene Undo/Redo and Save "
                                        "use the ordinary object workflow."))
                perform(true);
            FORGE_UI_PROBE("foliage:place");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (!error_.empty())
                field_error(error_);
            if (!status_.empty())
                ImGui::TextWrapped("%s", status_.c_str());
            ImGui::TextWrapped("Each click creates a new group. Move, edit or delete objects in "
                               "the Hierarchy and Inspector. Existing assets remain shared, while "
                               "template edits do not update previous groups. This tool does not "
                               "copy gameplay, animation, Prefab links or children. Mesh bounds "
                               "can overlap even when origins meet the minimum spacing.");
        }
        ImGui::End();
    }

  private:
    static const Json* find_mesh(const Json& view, const std::string& id) {
        for (const auto& row : view.at("entities"))
            if (row.at("id") == id && !row.value("prefab", false) &&
                row.at("components").contains("forge.mesh_renderer"))
                return &row;
        return nullptr;
    }
    AuthoringSnapshot snapshot_;
    const Scene* owner_ = nullptr;
    AssetId scene_;
    std::string source_, status_, error_;
    std::array<char, 128> name_{'F', 'o', 'l', 'i', 'a', 'g', 'e', 0};
    detail::MeshScatterSettings settings_;
    int count_ = 32;
};
} // namespace forge::ui
