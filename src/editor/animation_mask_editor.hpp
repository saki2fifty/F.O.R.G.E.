#pragma once
#include "../animation_mask_authoring.hpp"
#include "../animation_mask_binding.hpp"
#include "../texture_authoring.hpp"
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/authoring.hpp>
namespace forge {
class AnimationMaskEditor {
    friend struct AssetDocumentAutomation;

  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || open_error_; }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const AnimationMaskDocument* document() const { return document_.get(); }
    const std::string& diagnostic() const { return error_; }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() { save_ = true; }
    void request_close() {
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (can_undo()) {
            document_->undo();
            changed();
        }
    }
    void redo() {
        if (can_redo()) {
            document_->redo();
            changed();
        }
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        if (dirty()) {
            next_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            error_ = e.what();
            open_error_ = true;
        }
    }
    void content(SceneDocument& project, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New animation mask...",
                       "Create reusable weighted bone influences for a second animation clip."))
            ImGui::OpenPopup("New animation mask");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New animation mask", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ImGui::BeginDisabled(locked);
            if (ui::button("Create",
                           "Create the source document; choose an imported skeleton and bones.")) {
                try {
                    auto created = AnimationMaskDocument::create(project.writer_guard(),
                                                                 std::filesystem::u8path(path_));
                    const auto source = created->locator();
                    created.reset();
                    load(project, source);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    error_ = e.what();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Cancel", "Close without creating a file."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, std::string& message) {
        if (document_ && document_->project() != project.project()) {
            finish_close();
            next_source_.reset();
        }
        if (service_)
            for (auto& result : service_->poll()) {
                if (result.job.id != job_)
                    continue;
                job_ = 0;
                if (result.published) {
                    published_ = std::make_shared<const AssetCatalog>(result.publication->catalog);
                    catalog_ = published_;
                    needs_publish_ = false;
                    error_.clear();
                    message = "Animation mask saved and published.";
                } else {
                    needs_publish_ = true;
                    close_ = false;
                    error_ = result.diagnostic;
                }
            }
        if (close_ && !dirty())
            finish_close();
        if (!document_ && next_source_) {
            auto source = std::exchange(next_source_, {});
            open(project, *source);
        }
    }
    void draw(SceneDocument& project, Scene& scene, bool locked) {
        (void)project;
        if (!document_) {
            if (open_error_) {
                if (ImGui::Begin("Animation Mask###Animation Mask", &open_error_)) {
                    ui::field_error(error_);
                    if (ui::button("Close", "Close the failed source document."))
                        request_close();
                }
                ImGui::End();
            }
            return;
        }
        if (save_ && !locked && !pending()) {
            save_ = false;
            publish();
        }
        bool opened = true;
        const bool expanded = ImGui::Begin("Animation Mask###Animation Mask", &opened);
        if (!opened)
            request_close();
        if (document_ && ui::editor_context &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_mask",
                                                    path_utf8(document_->locator()));
        if (expanded && document_) {
            ImGui::TextWrapped(
                "Influence controls how much the second clip affects each bone: 0 keeps the first "
                "clip; 1 uses the full blend weight. A child entry overrides inherited influence.");
            ImGui::TextUnformatted(path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save / Publish",
                           "Save and cook. Failed publication retains the last working mask."))
                request_save();
            FORGE_UI_PROBE("animation_mask:save");
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo the mask source edit."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo the mask source edit."))
                redo();
            ImGui::EndDisabled();
            auto draft = document_->source().document;
            Json skeleton = draft.at("skeleton");
            if (asset_ref_picker(*catalog_, skeleton, SkeletonAsset::type, "Skeleton", false))
                mutate("Choose mask skeleton", [&](Json& d) { d["skeleton"] = skeleton; });
            float base = draft.at("default_weight");
            if (ImGui::InputFloat("Default influence", &base, 0, 0, "%.3f",
                                  ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Set default bone influence", [&](Json& d) { d["default_weight"] = base; });
            FORGE_UI_PROBE("animation_mask:default_weight");
            ui::help("Influence for bones without an inherited or explicit entry. Range 0–1. Press "
                     "Enter to commit.");
            ImGui::SeparatorText("Bone overrides");
            asset_ref_picker(*catalog_, new_joint_, ModelNodeAsset::type, "New bone", false, owner_,
                             &joints_);
            bool duplicate = false;
            for (const auto& row : draft.at("entries"))
                duplicate |= row.at("joint") == new_joint_;
            ImGui::BeginDisabled(new_joint_.is_null() || duplicate ||
                                 draft.at("entries").size() >= 1024);
            if (ui::button("Add bone", "Include this bone and descendants at full influence; "
                                       "adjust its weight below."))
                mutate("Add mask bone", [&](Json& d) {
                    d["entries"].push_back(
                        {{"joint", new_joint_}, {"weight", 1}, {"descendants", true}});
                });
            FORGE_UI_PROBE("animation_mask:add_bone");
            ImGui::EndDisabled();
            if (ImGui::BeginChild("Mask entries", {0, 220}, ImGuiChildFlags_Borders)) {
                ImGuiListClipper clipper;
                clipper.Begin(int(draft.at("entries").size()),
                              ImGui::GetTextLineHeightWithSpacing());
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const auto id =
                            draft.at("entries")[std::size_t(i)].at("joint").get<AssetId>();
                        const auto found = catalog_->records().find(id);
                        auto label = found == catalog_->records().end()
                                         ? id.str() + " (missing)"
                                         : asset_display(found->second);
                        if (!joints_.contains(id))
                            label += " (outside skeleton)";
                        if (ImGui::Selectable((label + "##" + id.str()).c_str(), selected_ == id))
                            selected_ = id;
                        FORGE_UI_PROBE("animation_mask:bone:" + id.str());
                    }
            }
            ImGui::EndChild();
            for (const auto& row : draft.at("entries"))
                if (selected_ && row.at("joint") == Json(selected_)) {
                    float influence = row.at("weight");
                    if (ImGui::InputFloat("Bone influence", &influence, 0, 0, "%.3f",
                                          ImGuiInputTextFlags_EnterReturnsTrue))
                        mutate_entry("Set bone influence", "weight", influence);
                    FORGE_UI_PROBE("animation_mask:weight");
                    bool descendants = row.at("descendants");
                    if (ImGui::Checkbox("Include descendants", &descendants))
                        mutate_entry("Set descendant scope", "descendants", descendants);
                    FORGE_UI_PROBE("animation_mask:descendants");
                    if (ui::button("Remove bone", "Remove this override; inherited or default "
                                                  "influence applies again.")) {
                        mutate("Remove mask bone", [&](Json& d) {
                            auto& entries = d["entries"];
                            std::erase_if(entries.get_ref<Json::array_t&>(), [&](const auto& e) {
                                return e.at("joint") == Json(selected_);
                            });
                        });
                        selected_ = {};
                    }
                    FORGE_UI_PROBE("animation_mask:remove_bone");
                    break;
                }
            ImGui::SeparatorText("Scene object");
            ImGui::BeginDisabled(needs_publish_ || document_->dirty());
            if (ui::button("Attach to selected Animator",
                           "Assign this mask to a compatible Animator's second clip in one Scene "
                           "Undo step."))
                attach(scene);
            FORGE_UI_PROBE("animation_mask:attach");
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing animation mask...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Source changes need Save / Publish.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (close_ && dirty())
            ImGui::OpenPopup("Pending animation mask changes");
        if (ImGui::BeginPopupModal("Pending animation mask changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Save and publish, discard the draft, or keep editing?");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Publish before closing.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::button("Discard", "Close without saving this draft.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Keep editing", "Cancel closing.")) {
                close_ = false;
                next_source_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

  private:
    std::unique_ptr<AnimationMaskDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::shared_ptr<const AssetCatalog> catalog_, published_;
    std::optional<std::filesystem::path> next_source_;
    std::optional<AssetId> owner_;
    std::set<AssetId> joints_;
    AssetJobId job_ = 0;
    AssetId selected_;
    Json new_joint_ = nullptr;
    bool needs_publish_ = false, save_ = false, close_ = false, open_error_ = false;
    char path_[256] = "Assets/UpperBody.mask.json";
    std::string error_;
    void finish_close() {
        document_.reset();
        service_.reset();
        catalog_.reset();
        joints_.clear();
        owner_ = AssetId{};
        job_ = 0;
        selected_ = {};
        new_joint_ = nullptr;
        needs_publish_ = save_ = close_ = open_error_ = false;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto doc = std::make_unique<AnimationMaskDocument>(project.writer_guard(), source);
        auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project.project()));
        auto service = std::make_unique<AssetImportService>(
            project.writer_guard(), animation_mask_import_registry(), desktop_texture_target());
        const auto at = catalog->records().find(doc->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    animation_mask_byte_limit);
        const bool needs = at == catalog->records().end() ||
                           !at->second.metadata.contains("forge.import") ||
                           at->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(doc);
        catalog_ = std::move(catalog);
        service_ = std::move(service);
        needs_publish_ = needs;
        error_.clear();
        refresh_joints();
        if (!document_->source().document.at("entries").empty())
            selected_ =
                document_->source().document.at("entries").front().at("joint").get<AssetId>();
    }
    void refresh_joints() {
        joints_.clear();
        owner_ = AssetId{};
        new_joint_ = nullptr;
        const auto& skeleton = document_->source().document.at("skeleton");
        if (skeleton.is_null())
            return;
        try {
            const auto id = skeleton.get<AssetId>();
            const auto resolved = catalog_->resolve(id, SkeletonAsset::type);
            if (resolved.state != AssetState::Available || !resolved.record->subasset)
                throw std::runtime_error("Choose a current skeleton from an imported model");
            const auto selected = asset_detail::load_model_selection(
                document_->project(), *catalog_, resolved.record->subasset->owner);
            const auto joints = asset_detail::animation_mask_joints(selected, id);
            owner_ = selected.owner;
            joints_.insert(joints.assets.begin(), joints.assets.end());
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void changed() {
        needs_publish_ = true;
        refresh_joints();
    }
    template <class F> void mutate(std::string label, F edit) {
        try {
            document_->edit(document_->revision(), std::move(label), edit);
            error_.clear();
            changed();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
    void mutate_entry(std::string label, const char* field, const Json& value) {
        mutate(std::move(label), [&](Json& d) {
            for (auto& row : d["entries"])
                if (row.at("joint") == Json(selected_))
                    row[field] = value;
        });
    }
    void publish() {
        try {
            document_->save();
            job_ = service_->submit(
                service_->prepare(document_->locator(), {}, document_->source().asset()),
                [](auto& c, const auto& p, const auto&) {
                    prepare_animation_mask_publication(c, p);
                },
                [](const auto&, const auto&) {});
        } catch (const std::exception& e) {
            needs_publish_ = true;
            close_ = false;
            error_ = e.what();
        }
    }
    void attach(Scene& scene) {
        try {
            if (!ui::editor_context)
                throw std::runtime_error("Choose a scene object with an Animator");
            const auto id = ui::editor_context->selection.entity();
            auto actor = scene.entity(id);
            if (!actor || !actor.has<Animator>())
                throw std::runtime_error("Choose a scene object with an Animator");
            const auto& config = actor.get<Animator>();
            if (config.states.id || !config.blend_clip.id ||
                Json(config.skeleton.id) != document_->source().document.at("skeleton"))
                throw std::runtime_error("Choose an Animator with this skeleton and a second clip; "
                                         "named states cannot use a direct bone mask");
            authoring_command(scene, "property.set",
                              {{"entity", id},
                               {"component", "forge.animator"},
                               {"field", "blend_mask"},
                               {"value", document_->source().asset()}});
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
} // namespace forge
