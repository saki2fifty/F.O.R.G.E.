#pragma once
#include "../animation_clip_selection.hpp"
#include "../animation_retarget_authoring.hpp"
#include "../texture_authoring.hpp"
#include "editor_state.hpp"
#include "property_drawer.hpp"
#include "source_document_panel.hpp"
#include <forge/authoring.hpp>
#include <future>

namespace forge {
class AnimationRetargetEditor : public SourceDocumentPanel<AnimationRetargetDocumentTraits> {
    friend struct AssetDocumentAutomation;

  public:
    AnimationRetargetEditor()
        : SourceDocumentPanel("Animation retarget", animation_retarget_import_registry,
                              prepare_animation_retarget_publication, desktop_texture_target()) {}
    ~AnimationRetargetEditor() override { inspection_stop_.request_stop(); }
    const std::string& diagnostic() const { return error_; }
    bool inspecting() const { return queued_ || inspection_job_.valid(); }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New animation retarget...", "Bake mapped motion for another skeleton."))
            ImGui::OpenPopup("New animation retarget");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New animation retarget", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            FORGE_UI_PROBE("animation_retarget:new_path");
            ImGui::BeginDisabled(locked);
            if (ui::button("Create",
                           "Create a recipe; choose skeletons, motion and joint pairs.")) {
                try {
                    auto document = AnimationRetargetDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_));
                    const auto path = document->locator();
                    document.reset();
                    load(project, path);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& error) {
                    error_ = error.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a recipe."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        SourceDocumentPanel::poll(project, message);
        if (inspection_job_.valid() &&
            inspection_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto value = inspection_job_.get();
                if (!inspection_stop_.stop_requested() && running_key_ == wanted_key_ &&
                    document_) {
                    inspected_ = std::move(value);
                    inspection_error_.clear();
                }
            } catch (const std::exception& error) {
                if (!inspection_stop_.stop_requested() && running_key_ == wanted_key_)
                    inspection_error_ = error.what();
            }
        }
        if (!inspection_job_.valid() && queued_ && document_ && catalog_) {
            queued_ = false;
            running_key_ = wanted_key_;
            inspection_stop_ = {};
            const auto from = document_->source().document.at("source_skeleton").get<AssetId>();
            const auto to = document_->source().document.at("target_skeleton").get<AssetId>();
            inspection_job_ =
                std::async(std::launch::async, [root = document_->project(), catalog = catalog_,
                                                from, to, stop = inspection_stop_.get_token()] {
                    auto source =
                        animation_detail::inspect_animation_skeleton(root, *catalog, {from}, stop);
                    auto target = from == to ? source
                                             : animation_detail::inspect_animation_skeleton(
                                                   root, *catalog, {to}, stop);
                    return Inspection{std::move(source), std::move(target)};
                });
        }
    }
    void draw(SceneDocument&, Scene& scene, bool locked) {
        if (!is_open())
            return;
        if (save_ && document_ && !locked && !pending()) {
            save_ = false;
            publish();
        }
        bool open = true;
        const bool expanded = ImGui::Begin("Animation Retarget", &open);
        if (!open)
            request_close();
        if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document(
                "animation_retarget", document_ ? path_utf8(document_->locator()) : "");
        if (expanded && document_) {
            ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
            ImGui::TextWrapped(
                "Bake joint rotations for the target skeleton. Target proportions "
                "and unmapped local rests stay fixed. Publish before using the clip.");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish", "Save the recipe and bake its target-bound clip."))
                request_save();
            FORGE_UI_PROBE("animation_retarget:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo a recipe edit."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo a recipe edit."))
                redo();
            ImGui::EndDisabled();
            const auto draft = document_->source().document;
            std::set<AssetId> skeletons;
            for (const auto& [id, record] : catalog_->records())
                if (record.type == SkeletonAsset::type && record.subasset &&
                    !record.subasset->removed)
                    skeletons.insert(id);
            reference(draft, "source_skeleton", "Source skeleton", SkeletonAsset::type, {},
                      &skeletons);
            AssetId source_owner;
            if (!draft.at("source_skeleton").is_null()) {
                const auto found =
                    catalog_->records().find(draft.at("source_skeleton").get<AssetId>());
                if (found != catalog_->records().end() && found->second.subasset)
                    source_owner = found->second.subasset->owner;
            }
            reference(draft, "source_clip", "Source clip", AnimationClipAsset::type, source_owner);
            reference(draft, "target_skeleton", "Target skeleton", SkeletonAsset::type, {},
                      &skeletons);
            ui::property_label_row("Samples per second", "Uniform baked samples, from 1 to 120.");
            int rate = draft.at("sampling_rate");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputInt("##rate", &rate, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Change retarget sampling rate",
                       [&](auto& d) { d["sampling_rate"] = rate; });
            FORGE_UI_PROBE("animation_retarget:rate");
            bool root = draft.at("root_translation");
            if (ImGui::Checkbox("Transfer root translation", &root))
                mutate("Set root translation", [&](auto& d) { d["root_translation"] = root; });
            FORGE_UI_PROBE("animation_retarget:root_translation");
            ui::help("Requires one mapped source/target root pair. Other translations and morphs "
                     "are not transferred; skeleton and sampled animation scales must be one.");
            double scale = draft.at("translation_scale");
            ui::property_label_row("Root translation scale",
                                   "Explicit displacement multiplier, 0 to 100.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputDouble("##translation-scale", &scale, 0, 0, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Scale root translation", [&](auto& d) { d["translation_scale"] = scale; });
            FORGE_UI_PROBE("animation_retarget:translation_scale");
            const auto q = draft.at("alignment").get<std::array<double, 4>>();
            auto degrees = rotation_to_euler({float(q[0]), float(q[1]), float(q[2]), float(q[3])});
            ui::property_label_row("Alignment (degrees)",
                                   "Rotate source motion into the target model's axes.");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputScalarN("##alignment", ImGuiDataType_Double, degrees.data(), 3, nullptr,
                                    nullptr, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                const auto rotation = rotation_from_euler(degrees);
                mutate("Align retarget motion", [&](auto& d) {
                    d["alignment"] = {rotation.x, rotation.y, rotation.z, rotation.w};
                });
            }
            FORGE_UI_PROBE("animation_retarget:alignment");
            ImGui::SeparatorText("Joint mapping");
            if (inspecting())
                ImGui::TextWrapped("Preparing published skeleton joints...");
            else if (!inspection_error_.empty())
                ui::field_error(inspection_error_);
            if (inspected_) {
                std::set<AssetId> sources(inspected_->source.joints.begin(),
                                          inspected_->source.joints.end());
                std::set<AssetId> targets(inspected_->target.joints.begin(),
                                          inspected_->target.joints.end());
                ui::property_label_row("Source joint",
                                       "Choose a joint from the selected source skeleton.");
                ImGui::SetNextItemWidth(-FLT_MIN);
                asset_ref_picker(*catalog_, new_source_, ModelNodeAsset::type, "##source-joint",
                                 false, inspected_->source.model, &sources);
                FORGE_UI_PROBE("animation_retarget:source_joint");
                ui::property_label_row("Target joint", "Choose its corresponding target joint.");
                ImGui::SetNextItemWidth(-FLT_MIN);
                asset_ref_picker(*catalog_, new_target_, ModelNodeAsset::type, "##target-joint",
                                 false, inspected_->target.model, &targets);
                FORGE_UI_PROBE("animation_retarget:target_joint");
                bool duplicate = false;
                for (const auto& entry : draft.at("entries"))
                    duplicate |=
                        entry.at("source") == new_source_ || entry.at("target") == new_target_;
                ImGui::BeginDisabled(new_source_.is_null() || new_target_.is_null() || duplicate ||
                                     draft.at("entries").size() >= animation_retarget_joint_limit);
                if (ui::button("Add joint pair",
                               "Map one source joint to one target joint. Map parents too."))
                    mutate("Map retarget joints", [&](auto& d) {
                        d["entries"].push_back({{"source", new_source_}, {"target", new_target_}});
                    });
                FORGE_UI_PROBE("animation_retarget:add_pair");
                ImGui::EndDisabled();
            }
            draw_pairs(draft);
            ImGui::BeginDisabled(draft.at("entries").empty());
            if (ui::button("Clear joint mapping", "Remove every pair in one recipe Undo step."))
                mutate("Clear retarget mapping", [](auto& d) { d["entries"] = Json::array(); });
            FORGE_UI_PROBE("animation_retarget:clear");
            ImGui::EndDisabled();
            ImGui::BeginDisabled(dirty());
            if (ui::button("Use on selected Animator",
                           "Assign the published clip to a matching direct Animator."))
                attach(scene);
            FORGE_UI_PROBE("animation_retarget:attach");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextWrapped("Baking and publishing...");
            else if (dirty())
                ImGui::TextWrapped("Recipe changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::TextWrapped(
                "Unit-scale rigs, matching mapped parent chains, up to 256 joints, "
                "60 seconds and 32768 sampled joints. Larger recipes reject safely. "
                "Use the selected Animator's preview or Play to inspect published motion.");
        } else if (expanded && !error_.empty())
            ui::field_error(error_);
        ImGui::End();
        draw_close_dialog("Pending animation retarget changes", locked);
    }

  private:
    struct Inspection {
        animation_detail::AnimationSkeletonInspection source, target;
    };
    char path_[512] = "Assets/Motion.retarget.json";
    Json new_source_ = nullptr, new_target_ = nullptr;
    std::optional<Inspection> inspected_;
    std::future<Inspection> inspection_job_;
    std::stop_source inspection_stop_;
    std::string wanted_key_, running_key_, inspection_error_;
    bool queued_ = false;
    void source_opened() override { refresh_inspection(); }
    void catalog_changed() override { refresh_inspection(); }
    void changed() override {
        SourceDocumentPanel::changed();
        refresh_inspection();
    }
    void view_closed() override {
        inspection_stop_.request_stop();
        wanted_key_.clear();
        queued_ = false;
        inspected_.reset();
    }
    void refresh_inspection() {
        if (!document_ || !catalog_)
            return;
        Json selection = Json::array({document_->session()});
        bool complete = true;
        for (const auto* field : {"source_skeleton", "target_skeleton"}) {
            const auto& id = document_->source().document.at(field);
            selection.push_back(id);
            if (id.is_null()) {
                complete = false;
                continue;
            }
            const auto found = catalog_->records().find(id.get<AssetId>());
            if (found == catalog_->records().end() ||
                !found->second.metadata.contains("forge.import"))
                complete = false;
            else
                selection.push_back(found->second.metadata.at("forge.import"));
        }
        const auto key = selection.dump();
        if (key == wanted_key_)
            return;
        wanted_key_ = key;
        inspection_stop_.request_stop();
        inspected_.reset();
        inspection_error_.clear();
        new_source_ = new_target_ = nullptr;
        queued_ = complete;
    }
    template <class F> void mutate(std::string label, F edit) {
        (void)edit_source(document_->revision(), std::move(label), edit);
    }
    void reference(const Json& draft, const char* field, const char* label, const char* type,
                   std::optional<AssetId> owner = {}, const std::set<AssetId>* allowed = nullptr) {
        ui::property_label_row(label, "Select the published asset used by this recipe.");
        auto value = draft.at(field);
        const auto key = std::string("##") + field;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (asset_ref_picker(*catalog_, value, type, key.c_str(), false, owner, allowed))
            mutate(std::string("Choose ") + label, [&](auto& d) { d[field] = value; });
        FORGE_UI_PROBE("animation_retarget:" + std::string(field));
    }
    void draw_pairs(const Json& draft) {
        if (!ImGui::BeginTable("Mapped joints", 3,
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp))
            return;
        ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60 * ui::interface_scale);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < draft.at("entries").size(); ++i) {
            ImGui::PushID(int(i));
            const auto& entry = draft.at("entries")[i];
            ImGui::TableNextRow();
            for (const auto* key : {"source", "target"}) {
                ImGui::TableNextColumn();
                const auto found = catalog_->records().find(entry.at(key).get<AssetId>());
                const auto label = found == catalog_->records().end()
                                       ? std::string("Missing joint")
                                       : asset_display(found->second);
                ImGui::TextWrapped("%s", label.c_str());
            }
            ImGui::TableNextColumn();
            const bool remove = ui::button("Remove", "Remove this explicit joint pair.");
            FORGE_UI_PROBE("animation_retarget:remove:" + std::to_string(i));
            ImGui::PopID();
            if (remove) {
                mutate("Remove retarget pair", [&](auto& d) { d["entries"].erase(i); });
                break;
            }
        }
        ImGui::EndTable();
    }
    void attach(Scene& scene) {
        try {
            if (!document_ || !ui::editor_context)
                throw std::runtime_error("Select a model root with an Animator first");
            const auto id = ui::editor_context->selection.entity();
            const auto actor = scene.entity(id);
            if (!actor || !actor.has<Animator>())
                throw std::runtime_error("Select a model root with an Animator first");
            const auto& config = actor.get<Animator>();
            if (config.states.id || config.blend_space.id || config.graph.id)
                throw std::runtime_error(
                    "Choose this clip in the active States, Blend Space or Animation Graph editor");
            (void)animation_detail::select_animation_clip(*catalog_, config.skeleton,
                                                          {document_->source().asset()});
            authoring_command(scene, "property.set",
                              {{"entity", id},
                               {"component", "forge.animator"},
                               {"field", "clip"},
                               {"value", document_->source().asset()}});
            error_.clear();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }
};
} // namespace forge
