#pragma once
#include "../animation_material_preview.hpp"
#include "../texture_authoring.hpp"
#include "../transform_animation_authoring.hpp"
#include "../transform_animation_preview.hpp"
#include "animation_sequence_editor.hpp"
#include "animation_timeline_view.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include "mesh_material_inspector.hpp"
#include "sprite_frame_context.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <optional>
#include <set>
#include <vector>
namespace forge {
class AnimationTimelineEditor : public ui::SequenceFrameSource {
  public:
    ui::SpriteFrameField sprite_frames;
    bool close_cancelled = false;
    bool is_open() const { return bool(document_) || sequence_.is_open(); }
    bool sequence_mode() const { return sequence_.is_open(); }
    void on_frame_export(std::function<void()> request, std::function<bool()> available) {
        sequence_.export_frames_requested = std::move(request);
        sequence_.export_frames_available = std::move(available);
    }
    bool frame_export_ready() const override {
        return sequence_mode() && sequence_.frame_export_ready();
    }
    AnimationSequenceEditor::FrameExportInfo frame_export_info() const override {
        return sequence_.frame_export_info();
    }
    Json frame_export_document() const override { return sequence_.frame_export_document(); }
    std::uint64_t begin_frame_export() override { return sequence_.begin_frame_export(); }
    bool frame_export_current(std::uint64_t token) const override {
        return sequence_mode() && sequence_.frame_export_current(token);
    }
    void sample_frame_export(std::uint64_t token, double seconds) override {
        sequence_.sample_frame_export(token, seconds);
    }
    void end_frame_export(std::uint64_t token) override { sequence_.end_frame_export(token); }
    bool take_scene_save_request() { return std::exchange(sequence_.save_requested, false); }
    bool pending() const { return job_ != 0; }
    bool dirty() const {
        if (sequence_mode())
            return sequence_.dirty();
        return document_ && (document_->dirty() || curve_dirty_ || needs_publish_ || pending());
    }
    bool can_undo() const {
        if (sequence_mode())
            return sequence_.can_undo();
        return document_ && !pending() &&
               (curve_dirty_ || curve_drag_ || box_ || pan_ || document_->can_undo());
    }
    bool can_redo() const {
        if (sequence_mode())
            return sequence_.can_redo();
        return document_ && !pending() && !curve_dirty_ && !curve_drag_ && !box_ && !pan_ &&
               document_->can_redo();
    }
    const TransformAnimationDocument* document() const { return document_.get(); }
    const std::set<AssetId>& selected_keys() const { return selected_keys_; }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void asset_catalog_changed(std::shared_ptr<const AssetCatalog> catalog) {
        if (!catalog || catalog == material_catalog_)
            return;
        sequence_.asset_catalog_changed(catalog);
        material_catalog_ = std::move(catalog);
        if (material_state_)
            material_state_->preview.refresh();
    }
    void request_save() {
        if (sequence_mode()) {
            sequence_.request_save();
            return;
        }
        cancel_ruler_capture();
        cancel_key_drag();
        cancel_curve_drag();
        save_ = true;
    }
    void request_close() {
        if (sequence_mode()) {
            sequence_.request_close();
            return;
        }
        cancel_ruler_capture();
        cancel_key_drag();
        cancel_curve_drag();
        stop_preview();
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
        if (sequence_mode()) {
            sequence_.undo();
            return;
        }
        if (box_ || pan_) {
            cancel_ruler_capture();
            return;
        }
        if (curve_drag_) {
            cancel_curve_drag();
            return;
        }
        if (curve_dirty_) {
            restore_curve_fields();
            return;
        }
        if (can_undo()) {
            document_->undo();
            changed();
        }
    }
    void redo() {
        if (sequence_mode()) {
            sequence_.redo();
            return;
        }
        if (can_redo()) {
            document_->redo();
            changed();
        }
    }
    bool preview_active() const {
        return sequence_mode() ? sequence_.preview_active()
                               : document_ && preview_ && target_.entity && clip_;
    }
    void stop_preview() {
        preview_ = playing_ = false;
        sequence_.stop_preview();
    }
    Json preview(Json source) {
        if (sequence_mode())
            return sequence_.preview(std::move(source));
        try {
            if (preview_active() && !clip_->material_bindings.empty() && !material_error_.empty())
                throw std::runtime_error(material_error_);
            if (preview_active() && !apply_material_animation_preview(source, target_, *clip_,
                                                                      time_, material_ready())) {
                stop_preview();
                error_ = "Preview target is no longer in this scene. Choose an object again.";
            }
        } catch (const AnimationMaterialPending& e) {
            material_status_ = e.what();
        } catch (const std::exception& e) {
            stop_preview();
            report("preview", e.what());
        }
        return source;
    }
    void open(SceneDocument& project, const std::filesystem::path& source) {
        next_sequence_.reset();
        if (sequence_mode()) {
            sequence_.request_close();
            if (sequence_mode()) {
                next_source_ = source;
                return;
            }
        }
        if (dirty()) {
            next_source_ = source;
            request_close();
            return;
        }
        try {
            load(project, source);
        } catch (const std::exception& e) {
            report("open", e.what());
        }
    }
    void content(SceneDocument& project, Scene& scene, bool locked) {
        const auto selected =
            ui::editor_context ? ui::editor_context->selection.entity() : std::string();
        const auto controller = !selected.empty() && scene.entity(selected) &&
                                scene.entity(selected).has<AnimationSequence>();
        ImGui::BeginDisabled(locked);
        if (ui::button("Create animation sequence", "Create a Scene-owned sequence controller.")) {
            try {
                const auto id =
                    authoring_command(scene, "entity.create", {{"recipe", "animation.sequence"}})
                        .at("selected")
                        .get<std::string>();
                if (ui::editor_context)
                    ui::editor_context->selection.select_entity(id);
                open_sequence(project, scene, {scene.asset_id(), EntityId::parse(id)});
            } catch (const std::exception& e) {
                report("sequence", e.what());
            }
        }
        ImGui::BeginDisabled(!controller);
        if (ui::button("Open sequence timeline", "Arrange clips for the selected controller."))
            open_sequence(project, scene, {scene.asset_id(), EntityId::parse(selected)});
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::BeginDisabled(locked || (document_ && dirty()) || sequence_.pending_fields());
        if (ui::button("New transform animation...",
                       "Create a reusable animation of local position, rotation and scale."))
            ImGui::OpenPopup("New transform animation");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New transform animation", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ui::help("Choose a new .animation.json file inside your project.");
            ImGui::BeginDisabled(locked || (document_ && dirty()) || sequence_.pending_fields());
            if (ui::button("Create", "Create the animation source and open its timeline.")) {
                try {
                    auto created = TransformAnimationDocument::create(
                        project.writer_guard(), std::filesystem::u8path(path_));
                    load(project, created->locator());
                    if (ui::editor_context && !ui::editor_context->selection.entity().empty())
                        bind(scene, ui::editor_context->selection.entity());
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    report("create", e.what());
                }
            }
            ImGui::EndDisabled();
            if (ui::button("Cancel", "Close without creating an animation."))
                ImGui::CloseCurrentPopup();
            if (!error_.empty())
                ui::field_error(error_);
            ImGui::EndPopup();
        }
    }
    void poll(SceneDocument& project, Scene& scene, bool play, double seconds,
              std::string& message) {
        if (sequence_mode()) {
            sequence_.poll(play, seconds);
            if (std::exchange(sequence_.close_cancelled, false)) {
                next_source_.reset();
                next_sequence_.reset();
                close_cancelled = true;
            }
            if (sequence_mode())
                return;
        }
        if (document_ && document_->project() != project.project()) {
            finish_close();
            next_source_.reset();
            next_sequence_.reset();
        }
        if (play) {
            stop_preview();
            cancel_curve_drag();
        }
        if (service_)
            for (auto& result : service_->poll()) {
                if (result.job.id != job_)
                    continue;
                job_ = 0;
                if (result.published) {
                    published_ = std::make_shared<const AssetCatalog>(result.publication->catalog);
                    needs_publish_ = false;
                    error_.clear();
                    message = "Transform animation saved and published.";

                } else {
                    needs_publish_ = true;
                    close_ = false;
                    report("publish", result.diagnostic);
                }
            }
        if (!document_ && next_source_) {
            const auto source = std::exchange(next_source_, {});
            open(project, *source);
        }
        if (!document_ && next_sequence_) {
            const auto target = std::exchange(next_sequence_, {});
            open_sequence(project, scene, *target);
        }
        if (!document_)
            return;
        if (target_.entity &&
            (scene.asset_id() != target_.scene || !scene.entity(target_.entity.str()))) {
            target_ = {};
            stop_preview();
        }
        prepare_materials(project, scene);
        if (preview_ && playing_ && clip_) {
            time_ += std::clamp(seconds, 0., .25) * animation_timebase * preview_speed_;
            if (preview_loop_)
                time_ = std::fmod(time_, double(clip_->duration));
            else if (time_ >= clip_->duration) {
                time_ = clip_->duration;
                playing_ = false;
            }
        }
    }
    void draw(SceneDocument& project, Scene& scene, bool locked) {
        if (sequence_mode()) {
            bool visible = true;
            const bool expanded =
                ImGui::Begin(dirty() ? "* Animation Timeline — Scene sequence###Animation Timeline"
                                     : "Animation Timeline — Scene sequence###Animation Timeline",
                             &visible);
            if (!visible)
                sequence_.request_close();
            if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                ui::editor_context->task.focus_document("animation_timeline", "Animation Timeline");
            if (expanded && sequence_mode())
                sequence_.draw(locked);
            sequence_.close_dialog();
            ImGui::End();
            return;
        }
        if (!document_)
            return;
        bool visible = true;
        const auto title = std::string(dirty() ? "* Animation Timeline###Animation Timeline"
                                               : "Animation Timeline###Animation Timeline");
        const bool expanded = ImGui::Begin(title.c_str(), &visible);
        if (!visible)
            request_close();
        if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_timeline", "Animation Timeline");
        if ((box_ || pan_) && (locked || pending() || ImGui::GetIO().AppFocusLost || !document_ ||
                               ruler_revision_ != document_->revision() ||
                               !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
                               ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) ||
                               ImGui::IsKeyPressed(ImGuiKey_Escape)))
            cancel_ruler_capture();
        if (drag_key_ && (locked || pending() || ImGui::GetIO().AppFocusLost || !document_ ||
                          drag_revision_ != document_->revision() ||
                          ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) ||
                          !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
            cancel_key_drag();
        if (curve_drag_ &&
            (locked || pending() || ImGui::GetIO().AppFocusLost || !document_ ||
             curve_drag_revision_ != document_->revision() || curve_drag_key_ != key_ ||
             curve_drag_axis_ != curve_axis_ || curve_drag_channel_ != curve_channel_ ||
             curve_drag_material_slot_ != curve_material_slot_ ||
             curve_drag_material_key_ != curve_material_key_ ||
             !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
             ImGui::IsKeyPressed(ImGuiKey_Escape)))
            cancel_curve_drag();
        if (expanded && document_) {
            ImGui::TextWrapped("%s", path_utf8(document_->locator()).c_str());
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save & Publish",
                           "Save the source and publish the clip for Play and export."))
                request_save();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_undo());
            if (ui::button("Undo", "Undo one animation edit."))
                undo();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_redo());
            if (ui::button("Redo", "Redo one animation edit."))
                redo();
            ImGui::EndDisabled();
            target_controls(scene);
            double duration = double(clip_->duration) / animation_timebase;
            if (ImGui::InputDouble("Duration (seconds)", &duration, .1, 1, "%.3f",
                                   ImGuiInputTextFlags_EnterReturnsTrue))
                mutate("Change animation duration", [&](auto& source) {
                    source.document["duration_us"] = microseconds(duration);
                });
            ImGui::BeginDisabled(bool(drag_key_) || curve_drag_);
            ImGui::InputDouble("Retimed duration (seconds)", &retimed_seconds_, .1, 1, "%.6f");
            FORGE_UI_PROBE("animation:retimed_duration");
            ui::help(
                "Scale the entire clip to this duration, including key times and curve slopes. "
                "The Duration field above changes the end time without moving keys.");
            if (ui::button("Scale whole clip timing",
                           "Stretch or compress all keys as one Undo. "
                           "Reject colliding times or invalid curves.")) {
                if (mutate("Retime animation duration",
                           [&](auto& source) {
                               source.retime_duration(microseconds(retimed_seconds_));
                           }) &&
                    key_)
                    activate_key(key_);
            }
            FORGE_UI_PROBE("animation:retime");
            ImGui::EndDisabled();
            channels_controls();
            property_controls(scene);
            material_controls(scene);
            double seconds = time_ / animation_timebase;
            if (ImGui::SliderScalar("Playhead (seconds)", ImGuiDataType_Double, &seconds, &zero_,
                                    &duration, "%.3f")) {
                cancel_curve_drag();
                if (apply_pending_curves()) {
                    time_ = seconds * animation_timebase;
                    key_ = {};
                    selected_keys_.clear();
                    sample_fields(time_);
                    preview_ = bool(target_.entity);
                    playing_ = false;
                }
            }
            FORGE_UI_PROBE("animation:playhead");
            if (ui::button(
                    playing_ ? "Pause preview" : "Play preview",
                    "Animate the chosen object in Scene without changing its authored values.")) {
                if (target_.entity) {
                    if (time_ >= clip_->duration)
                        time_ = 0;
                    preview_ = true;
                    playing_ = !playing_;
                } else
                    error_ = "Choose a preview object first.";
            }
            ImGui::SameLine();
            if (ui::button("Stop preview", "Restore the authored Scene view."))
                stop_preview();
            ImGui::SameLine();
            ImGui::Checkbox("Loop preview", &preview_loop_);
            ImGui::SliderFloat("Preview speed", &preview_speed_, 0, 4, "%.2fx");
            ImGui::TextColored(preview_ ? ImVec4{.4f, .85f, 1, 1} : ImVec4{.6f, .65f, .7f, 1},
                               preview_ ? "PREVIEW — temporary view; Scene source is unchanged"
                                        : "Preview stopped — authored Scene view");
            view_controls();
            ruler();
            key_controls(scene);
            ImGui::EndDisabled();
            if (pending())
                ImGui::TextUnformatted("Publishing animation...");
            else if (needs_publish_)
                ImGui::TextUnformatted("Save & Publish to update Play and exported games.");
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
        if (save_ && document_ && !locked && !pending()) {
            save_ = false;
            try {
                if (!apply_pending_curves()) {
                    close_ = false;
                    throw std::runtime_error(error_);
                }
                document_->save();
                auto draft =
                    service_->prepare(document_->locator(), {}, document_->source().asset());
                job_ = service_->submit(
                    std::move(draft),
                    [](auto& c, const auto& p, const auto&) {
                        prepare_transform_animation_publication(c, p);
                    },
                    [](const auto&, const auto&) {});
            } catch (const std::exception& e) {
                close_ = false;
                report("save", e.what());
            }
        }
        if (close_ && dirty())
            ImGui::OpenPopup("Pending animation changes");
        if (ImGui::BeginPopupModal("Pending animation changes", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            if (!dirty())
                ImGui::CloseCurrentPopup();
            ImGui::TextWrapped(
                "Save and publish this animation, discard the draft, or keep editing?");
            ImGui::BeginDisabled(locked || pending());
            if (ui::button("Save", "Publish before closing.")) {
                request_save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::button("Discard", "Close without saving the draft.")) {
                finish_close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ui::button("Keep editing", "Cancel closing this animation.")) {
                close_ = false;
                next_source_.reset();
                next_sequence_.reset();
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (close_ && !dirty())
            finish_close();
    }
    // Domain actions shared with interaction tests. Scene attachment has Scene history;
    // keys have source-document history.
    void bind(Scene& scene, const std::string& id) {
        const auto entity = scene.entity(id);
        if (!entity || entity.has(flecs::Prefab))
            throw std::runtime_error("Choose a scene object for animation preview");
        const auto before = target_;
        target_ = {scene.asset_id(), EntityId::parse(id)};
        try {
            capture(scene, false);
        } catch (...) {
            target_ = before;
            throw;
        }
    }
    void open_sequence(SceneDocument& project, Scene& scene, EntityRef target) {
        if (sequence_mode() && sequence_.controller() == target)
            return;
        next_source_.reset();
        if (sequence_mode() && sequence_.pending_fields()) {
            next_sequence_ = target;
            sequence_.request_close();
            return;
        }
        if (document_ && dirty()) {
            next_sequence_ = target;
            request_close();
            return;
        }
        finish_close();
        try {
            sequence_.open(project, scene, target);
        } catch (const std::exception& e) {
            report("sequence", e.what());
        }
    }

  private:
    AnimationSequenceEditor sequence_;
    std::optional<EntityRef> next_sequence_;
    std::unique_ptr<TransformAnimationDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::optional<TransformAnimationClip> clip_;
    std::shared_ptr<const AssetCatalog> published_;
    std::optional<std::filesystem::path> next_source_;
    EntityRef target_{};
    // Resource pools keep stable addresses when this view is moved/reset.
    // Construct their private editing scope only when material controls need it.
    struct MaterialState {
        MeshMaterialInspector inspector;
        AnimationMaterialPreview preview;
    };
    std::unique_ptr<MaterialState> material_state_;
    std::shared_ptr<const AssetCatalog> material_catalog_;
    std::string material_slot_, material_status_, material_error_;
    bool material_open_ = false;
    std::vector<AnimationMaterialValues> material_fields_;
    std::vector<AnimationMaterialKey> material_curves_, curve_drag_materials_before_;
    std::string curve_material_slot_, curve_material_key_;
    std::string curve_drag_material_slot_, curve_drag_material_key_;
    AssetId key_{}, drag_key_{};
    std::set<AssetId> selected_keys_;
    std::vector<AssetId> drag_keys_;
    std::int64_t drag_origin_ = 0, drag_offset_ = 0, drag_min_ = 0, drag_max_ = 0;
    std::uint64_t drag_revision_ = 0;
    float drag_pointer_x_ = 0;
    bool drag_moved_ = false;
    double key_offset_seconds_ = 0;
    double value_position_multiplier_[3]{1, 1, 1}, value_position_offset_[3]{},
        value_scale_multiplier_[3]{1, 1, 1};
    double retimed_seconds_ = 2;
    int frame_grid_fps_ = 30;
    AnimationTimelineView view_, pan_before_;
    bool box_ = false, pan_ = false;
    bool box_updated_ = false;
    int box_mode_ = 0;
    ImVec2 ruler_origin_{}, ruler_extent_{}, ruler_pointer_{};
    ImVec2 box_last_{};
    std::uint64_t ruler_revision_ = 0;
    std::set<AssetId> box_before_;
    AssetId box_active_;
    double box_time_ = 0;
    bool box_preview_ = false, box_playing_ = false;
    AssetJobId job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false, preview_ = false, playing_ = false,
         preview_loop_ = true;
    double time_ = 0, zero_ = 0;
    float preview_speed_ = 1;
    double position_[3]{};
    AnimationPropertyValues property_fields_{};
    float rotation_[3]{}, scale_[3]{1, 1, 1};
    bool held_ = false, curve_dirty_ = false;
    TransformAnimationCurves curves_;
    int curve_channel_ = 0, curve_axis_ = 0;
    bool curve_fit_handles_ = false, curve_drag_ = false, curve_drag_incoming_ = false,
         curve_drag_moved_ = false, curve_drag_prior_dirty_ = false;
    AssetId curve_drag_key_{};
    std::uint64_t curve_drag_revision_ = 0;
    int curve_drag_axis_ = 0, curve_drag_channel_ = 0;
    TransformAnimationCurves curve_drag_before_;
    float curve_drag_pointer_y_ = 0;
    ImVec2 curve_drag_origin_{}, curve_drag_size_{};
    double curve_drag_slope_ = 0, curve_drag_per_pixel_ = 0, curve_drag_min_ = 0,
           curve_drag_max_ = 1;
    char path_[1024] = "Assets/NewAnimation.animation.json";
    std::string error_;
    static std::int64_t microseconds(double seconds) {
        if (!std::isfinite(seconds) || seconds < 0 || seconds > 3600)
            throw std::runtime_error("Animation time must be within 0–3600 seconds");
        return std::llround(seconds * animation_timebase);
    }
    void report(const char* action, const std::string& why) {
        error_ = why;
        ui::report_error(std::string("animation_timeline/") + action, why);
    }
    void finish_close() {
        sequence_.close();
        box_ = pan_ = false;
        box_before_.clear();
        view_ = {};
        curve_drag_ = false;
        curve_fit_handles_ = false;
        stop_preview();
        material_state_.reset();
        material_catalog_.reset();
        material_fields_.clear();
        material_curves_.clear();
        curve_drag_materials_before_.clear();
        curve_material_slot_.clear();
        curve_material_key_.clear();
        material_slot_.clear();
        material_status_.clear();
        material_error_.clear();
        material_open_ = false;
        document_.reset();
        clip_.reset();
        service_.reset();
        target_ = {};
        key_ = drag_key_ = {};
        selected_keys_.clear();
        drag_keys_.clear();
        drag_offset_ = 0;
        drag_revision_ = 0;
        curves_ = {};
        curve_dirty_ = false;
        job_ = 0;
        save_ = close_ = needs_publish_ = false;
    }
    void load(SceneDocument& project, const std::filesystem::path& source) {
        auto next = std::make_unique<TransformAnimationDocument>(project.writer_guard(), source);
        auto service = std::make_unique<AssetImportService>(project.writer_guard(),
                                                            transform_animation_import_registry(),
                                                            desktop_texture_target());
        const auto catalog = AssetCatalog::open_project(project.project());
        const auto at = catalog.records().find(next->source().asset());
        const auto bytes = asset_detail::read_bytes(ProjectPaths(project.project()).resolve(source),
                                                    transform_animation_byte_limit);
        const bool needs = at == catalog.records().end() ||
                           !at->second.metadata.contains("forge.import") ||
                           at->second.metadata.at("forge.import").at("source_digest") !=
                               asset_detail::content_digest(bytes);
        finish_close();
        document_ = std::move(next);
        service_ = std::move(service);
        material_catalog_ = std::make_shared<const AssetCatalog>(catalog);
        needs_publish_ = needs;
        changed();
        needs_publish_ = needs;
        time_ = 0;
        retimed_seconds_ = double(clip_->duration) / animation_timebase;
        select_key(clip_->keys.front().id);
        error_.clear();
    }
    void changed() {
        cancel_ruler_capture();
        curve_drag_ = false;
        clip_ = document_->source().clip();
        view_.reconcile(double(clip_->duration));
        needs_publish_ = true;
        time_ = std::min(time_, double(clip_->duration));
        material_fields_.resize(clip_->material_bindings.size());
        clip_->sample_materials(time_, material_fields_);
        if (key_) {
            const auto at = std::find_if(clip_->keys.begin(), clip_->keys.end(),
                                         [&](const auto& k) { return k.id == key_; });
            if (at == clip_->keys.end())
                key_ = {};
            else {
                held_ = at->held;
                curves_ = at->curves;
                curve_dirty_ = false;
                fields(at->pose, at->properties);
                fields_materials(at->materials);
            }
        }
        std::set<AssetId> surviving;
        for (const auto& key : clip_->keys)
            surviving.insert(key.id);
        std::erase_if(selected_keys_, [&](AssetId id) { return !surviving.contains(id); });
        drag_key_ = {};
        drag_keys_.clear();
        drag_offset_ = 0;
        drag_revision_ = 0;
    }
    template <class F> bool mutate(std::string label, F edit, bool include_pending = true) {
        try {
            const auto revision = document_->revision();
            document_->edit(revision, std::move(label), [&](Json& draft) {
                TransformAnimationSource source{draft};
                if (include_pending && curve_dirty_ && key_)
                    for (const auto& key : source.clip().keys)
                        if (key.id == key_) {
                            write_curve_fields(source, key);
                            break;
                        }
                edit(source);
                draft = std::move(source.document);
            });
            if (document_->revision() != revision)
                changed();
            else if (include_pending)
                curve_dirty_ = false;
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            report("edit", e.what());
            return false;
        }
    }
    void fields(const LocalTransform& pose, const AnimationPropertyValues& properties = {}) {
        property_fields_ = properties;
        position_[0] = pose.translation.x;
        position_[1] = pose.translation.y;
        position_[2] = pose.translation.z;
        const auto angles = rotation_to_euler(pose.rotation);
        for (int i = 0; i < 3; ++i)
            rotation_[i] = float(angles[i]);
        scale_[0] = pose.scale.x;
        scale_[1] = pose.scale.y;
        scale_[2] = pose.scale.z;
    }
    void fields_materials(std::span<const AnimationMaterialKey> materials) {
        material_curves_.assign(materials.begin(), materials.end());
        material_fields_.clear();
        for (const auto& value : materials)
            material_fields_.push_back(value.values);
    }
    void select_key(AssetId id) {
        cancel_curve_drag();
        if (!apply_pending_curves())
            return;
        selected_keys_.clear();
        selected_keys_.insert(id);
        activate_key(id);
    }
    void toggle_key(AssetId id) {
        cancel_curve_drag();
        if (!apply_pending_curves())
            return;
        if (selected_keys_.erase(id)) {
            if (key_ == id) {
                key_ = {};
                for (const auto& key : clip_->keys)
                    if (selected_keys_.contains(key.id)) {
                        activate_key(key.id);
                        break;
                    }
            }
        } else {
            selected_keys_.insert(id);
            activate_key(id);
        }
    }
    void cancel_key_drag() {
        if (drag_key_ && clip_)
            activate_key(drag_key_);
        drag_key_ = {};
        drag_keys_.clear();
        drag_offset_ = 0;
        drag_revision_ = 0;
    }
    static constexpr int material_curve_channel = int(animation_property_count) + 2;
    bool scalar_curve() const {
        return curve_channel_ >= 2 && curve_channel_ < material_curve_channel;
    }
    bool material_curve() const { return curve_channel_ == material_curve_channel; }
    bool vector_curve() const { return curve_channel_ < 2; }
    std::size_t material_curve_index() const {
        const auto at = std::find_if(clip_->material_bindings.begin(),
                                     clip_->material_bindings.end(), [&](const auto& value) {
                                         return value.slot == curve_material_slot_ &&
                                                value.key == curve_material_key_;
                                     });
        if (at == clip_->material_bindings.end())
            throw std::runtime_error("Material curve binding is unavailable");
        return std::size_t(at - clip_->material_bindings.begin());
    }
    AnimationProperty curve_property() const { return AnimationProperty(curve_channel_ - 2); }
    double curve_display_multiplier() const {
        return scalar_curve() ? animation_property_binding(curve_property()).display_multiplier : 1;
    }
    bool curve_cubic(const TransformAnimationCurves& value) const {
        if (material_curve())
            return material_curves_.at(material_curve_index()).curves.at(curve_axis_).cubic;
        return scalar_curve() ? (value.property_mask & animation_property_bit(curve_property())) &&
                                    value.properties[unsigned(curve_property())].cubic
                              : value.cubic;
    }
    bool curve_cubic(const TransformAnimationKey& value) const {
        return material_curve()
                   ? value.materials.at(material_curve_index()).curves.at(curve_axis_).cubic
                   : curve_cubic(value.curves);
    }
    double& curve_slope(bool incoming) {
        if (material_curve()) {
            auto& value = material_curves_.at(material_curve_index()).curves.at(curve_axis_);
            return incoming ? value.incoming : value.outgoing;
        }
        if (scalar_curve()) {
            auto& value = curves_.properties[unsigned(curve_property())];
            return incoming ? value.incoming : value.outgoing;
        }
        auto& side = incoming ? curves_.incoming : curves_.outgoing;
        return (curve_channel_ == 0 ? side.translation : side.scale)[curve_axis_];
    }
    void restore_curve_fields() {
        if (clip_)
            for (const auto& key : clip_->keys)
                if (key.id == key_) {
                    curves_ = key.curves;
                    material_curves_ = key.materials;
                }
        curve_dirty_ = false;
    }
    void cancel_curve_drag() {
        if (!curve_drag_)
            return;
        curves_ = curve_drag_before_;
        material_curves_ = curve_drag_materials_before_;
        curve_dirty_ = curve_drag_prior_dirty_;
        curve_drag_ = false;
    }
    bool apply_pending_curves() {
        if (!curve_dirty_)
            return true;
        if (!document_ || !clip_ || !key_)
            return false;
        const auto found = std::find_if(clip_->keys.begin(), clip_->keys.end(),
                                        [&](const auto& key) { return key.id == key_; });
        if (found == clip_->keys.end())
            return false;
        const auto key = *found;
        if (!mutate(
                "Set animation curves", [&](auto& source) { write_curve_fields(source, key); },
                false))
            return false;
        curve_dirty_ = false;
        return true;
    }
    static bool same_curve(const AnimationPropertyCurve& a, const AnimationPropertyCurve& b) {
        return a.cubic == b.cubic && a.incoming == b.incoming && a.outgoing == b.outgoing;
    }
    static bool same_curves(const TransformAnimationCurves& a, const TransformAnimationCurves& b) {
        if (a.cubic != b.cubic || a.property_mask != b.property_mask ||
            a.incoming.translation != b.incoming.translation ||
            a.incoming.scale != b.incoming.scale ||
            a.outgoing.translation != b.outgoing.translation ||
            a.outgoing.scale != b.outgoing.scale)
            return false;
        for (unsigned i = 0; i < animation_property_count; ++i)
            if (!same_curve(a.properties[i], b.properties[i]))
                return false;
        return true;
    }
    std::vector<AnimationMaterialCurveEdit>
    material_curve_edits(const TransformAnimationKey& key) const {
        std::vector<AnimationMaterialCurveEdit> edits;
        if (material_curves_.size() != clip_->material_bindings.size())
            throw std::runtime_error("Pending material curve definitions changed");
        for (std::size_t i = 0; i < material_curves_.size(); ++i)
            for (unsigned lane = 0;
                 lane < material_parameter_width(clip_->material_bindings[i].type); ++lane)
                if (!same_curve(key.materials.at(i).curves[lane], material_curves_[i].curves[lane]))
                    edits.push_back(
                        {clip_->material_bindings[i], lane, material_curves_[i].curves[lane]});
        return edits;
    }
    void write_curve_fields(TransformAnimationSource& source,
                            const TransformAnimationKey& key) const {
        if (!same_curves(key.curves, curves_))
            source.set_key(key.time, key.pose, key.held, key.id, curves_);
        const auto edits = material_curve_edits(key);
        if (!edits.empty())
            source.set_material_curves(key.id, edits);
    }
    void activate_key(AssetId id) {
        for (const auto& key : clip_->keys)
            if (key.id == id) {
                key_ = id;
                time_ = double(key.time);
                held_ = key.held;
                curves_ = key.curves;
                curve_dirty_ = false;
                fields(key.pose, key.properties);
                fields_materials(key.materials);
                return;
            }
    }
    void capture(Scene& scene, bool materials = true) {
        const auto entity = scene.entity(target_.entity.str());
        if (!entity || scene.asset_id() != target_.scene)
            throw std::runtime_error("Animation preview object is unavailable");
        const auto properties = AnimationPropertyState::from(entity, clip_->properties);
        AnimationPropertyValues captured{};
        for (const auto& binding : animation_property_bindings())
            if (clip_->properties & animation_property_bit(binding.property)) {
                const auto value = properties.value(binding.property);
                validate_animation_property(binding.property, value);
                captured[unsigned(binding.property)] = value;
            }
        auto captured_materials = material_fields_;
        if (materials && !clip_->material_bindings.empty()) {
            if (!entity.has<MeshRenderer>() || !material_ready())
                throw AnimationMaterialPending(
                    "Prepare the selected object's materials before capture");
            captured_materials.clear();
            for (const auto& binding : clip_->material_bindings) {
                const auto value =
                    material_state().preview.current(binding, entity.get<MeshRenderer>());
                AnimationMaterialValues numbers{};
                for (unsigned lane = 0; lane < material_parameter_width(value.type); ++lane)
                    numbers[lane] = value.value[lane];
                captured_materials.push_back(numbers);
            }
        }
        fields(
            {entity.has<LocalTranslation>() ? entity.get<LocalTranslation>() : LocalTranslation{},
             entity.has<LocalRotation>() ? entity.get<LocalRotation>() : LocalRotation{},
             entity.has<LocalScale>() ? entity.get<LocalScale>() : LocalScale{}},
            captured);
        material_fields_ = std::move(captured_materials);
    }
    void target_controls(Scene& scene) {
        std::string label = "Choose object";
        const auto source = scene.document();
        for (const auto& e : source.at("entities"))
            if (e.at("id") == target_.entity.str() && scene.asset_id() == target_.scene)
                label = e.at("name");
        if (ImGui::BeginCombo("Preview object", label.c_str())) {
            for (const auto& e : source.at("entities")) {
                if (e.value("prefab", false))
                    continue;
                const auto id = e.at("id").get<std::string>();
                ImGui::PushID(id.c_str());
                if (ImGui::Selectable(e.at("name").get_ref<const std::string&>().c_str(),
                                      id == target_.entity.str())) {
                    try {
                        bind(scene, id);
                    } catch (const std::exception& ex) {
                        report("bind", ex.what());
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!target_.entity || needs_publish_);
        if (ui::button("Attach animation to object",
                       "Add Transform Animator with this published clip as one Scene Undo step.")) {
            try {
                const auto id = target_.entity.str();
                auto properties = AnimationPropertyState::from(scene.entity(id), clip_->properties);
                properties.validate_clip(*clip_);
                properties.sample(clip_->properties, clip_->sample_properties(0));
                auto commands = Json::array();
                if (!scene.entity(id).has<TransformAnimator>())
                    commands.push_back(
                        {{"operation", "component.add"},
                         {"arguments",
                          {{"entity", id}, {"component", "forge.transform_animator"}}}});
                commands.push_back({{"operation", "property.set"},
                                    {"arguments",
                                     {{"entity", id},
                                      {"component", "forge.transform_animator"},
                                      {"field", "clip"},
                                      {"value", document_->source().asset()}}}});
                apply_authoring(scene, commands, scene.revision());
            } catch (const std::exception& e) {
                report("attach", e.what());
            }
        }
        ImGui::EndDisabled();
    }
    void channels_controls() {
        unsigned channels = clip_->channels;
        for (const auto& item : std::array<std::pair<const char*, unsigned>, 3>{
                 {{"Position", 1}, {"Rotation", 2}, {"Scale", 4}}}) {
            bool enabled = bool(channels & item.second);
            if (ImGui::Checkbox(item.first, &enabled)) {
                const auto next = enabled ? channels | item.second : channels & ~item.second;
                mutate("Change animated channels", [&](auto& source) {
                    auto list = Json::array();
                    if (next & 1)
                        list.push_back("forge.local_translation");
                    if (next & 2)
                        list.push_back("forge.local_rotation");
                    if (next & 4)
                        list.push_back("forge.local_scale");
                    source.document["channels"] = std::move(list);
                });
            }
            FORGE_UI_PROBE(std::string("animation:channel:") + item.first);
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    void sample_fields(double time) {
        fields(clip_->sample(time), clip_->sample_properties(time));
        material_fields_.resize(clip_->material_bindings.size());
        clip_->sample_materials(time, material_fields_);
    }
    MaterialState& material_state() {
        if (!material_state_)
            material_state_ = std::make_unique<MaterialState>();
        return *material_state_;
    }
    MeshMaterialInspector& material_inspector() { return material_state().inspector; }
    const asset_detail::PreparedModelDraw* material_ready() const {
        return material_state_ ? material_state_->preview.ready() : nullptr;
    }
    void prepare_materials(SceneDocument& project, Scene& scene) {
        if (!material_open_ && clip_->material_bindings.empty()) {
            material_state_.reset();
            material_status_.clear();
            return;
        }
        if (!target_.entity) {
            if (material_state_)
                material_state_->preview.reset();
            material_status_.clear();
            return;
        }
        try {
            const auto actor = scene.entity(target_.entity.str());
            if (target_.scene != scene.asset_id() || !actor || !actor.has<MeshRenderer>())
                throw std::runtime_error(
                    "Choose a preview object with a Mesh Renderer for material tracks.");
            const auto& renderer = actor.get<MeshRenderer>();
            material_inspector().select(project.project(), material_catalog_, renderer.mesh,
                                        renderer.materials, renderer.material_variant);
            material_inspector().poll();
            if (!material_inspector().parameter_slots().contains(material_slot_) &&
                !material_inspector().parameter_slots().empty())
                material_slot_ = material_inspector().parameter_slots().begin()->first;
            material_inspector().inspect_parameters(material_slot_);
            const bool ready = material_state().preview.prepare(project.project(), renderer, *clip_,
                                                                document_->revision());
            material_status_ = ready ? "" : "Preparing selected material definitions...";
            material_error_.clear();
        } catch (const std::exception& e) {
            material_error_ = e.what();
        }
    }
    void material_controls(Scene& scene) {
        material_open_ = ImGui::CollapsingHeader("Animated material values");
        FORGE_UI_PROBE("animation:material_properties");
        if (!material_open_)
            return;
        ImGui::TextWrapped("Animate values on this object by named material slot. Shared material "
                           "assets stay unchanged.");
        if (!material_status_.empty())
            ImGui::TextDisabled("%s", material_status_.c_str());
        if (!material_error_.empty())
            ui::field_error(material_error_);
        if (!material_inspector().error().empty())
            ui::field_error(material_inspector().error());
        if (!material_inspector().parameter_error().empty())
            ui::field_error(material_inspector().parameter_error());
        ImGui::BeginDisabled(bool(drag_key_) || curve_drag_ || box_ || pan_);
        std::optional<AnimationMaterialBinding> removed;
        for (const auto& binding : clip_->material_bindings) {
            ui::IdScope slot(binding.slot.c_str());
            ui::IdScope key(binding.key.c_str());
            ImGui::TextWrapped("%s / %s", binding.slot.c_str(),
                               material_field_label(binding.key).c_str());
            ImGui::SameLine();
            if (ui::button("Remove track", "Remove only this material track; preserve other tracks "
                                           "and source extensions."))
                removed = binding;
            FORGE_UI_PROBE("animation:material_remove:" + binding.slot + ":" + binding.key);
        }
        if (removed) {
            mutate("Remove animated material value", [&](auto& source) {
                source.erase_material_parameter(removed->slot, removed->key);
            });
            ImGui::EndDisabled();
            return;
        }
        if (ImGui::BeginCombo("Material slot", material_slot_.c_str())) {
            for (const auto& [slot, materials] : material_inspector().parameter_slots()) {
                (void)materials;
                if (ImGui::Selectable(slot.c_str(), slot == material_slot_))
                    material_slot_ = slot;
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation:material_slot");
        material_inspector().inspect_parameters(material_slot_);
        const auto actor = target_.entity && target_.scene == scene.asset_id()
                               ? scene.entity(target_.entity.str())
                               : flecs::entity{};
        ImGui::BeginDisabled(!actor || !actor.has<MeshRenderer>() ||
                             !material_inspector().parameters_ready());
        for (const auto& [key, initial] : material_inspector().parameter_definitions()) {
            const auto selected =
                std::find_if(clip_->material_bindings.begin(), clip_->material_bindings.end(),
                             [&](const auto& binding) {
                                 return binding.slot == material_slot_ && binding.key == key;
                             });
            bool enabled = selected != clip_->material_bindings.end();
            ui::IdScope id(key.c_str());
            if (ImGui::Checkbox(material_inspector().parameter_labels().at(key).c_str(),
                                &enabled)) {
                const AnimationMaterialBinding binding{material_slot_, key, initial.type};
                if (enabled)
                    mutate("Add animated material value", [&](auto& source) {
                        auto value = initial;
                        for (const auto& current : actor.get<MeshRenderer>().material_parameters)
                            if (current.slot == binding.slot && current.key == binding.key) {
                                if (current.parameter.type != binding.type)
                                    throw std::runtime_error("Existing material override has an "
                                                             "incompatible semantic type.");
                                value = current.parameter;
                            }
                        AnimationMaterialValues numbers{};
                        for (unsigned lane = 0; lane < material_parameter_width(value.type); ++lane)
                            numbers[lane] = value.value[lane];
                        source.add_material_parameter(binding, numbers);
                    });
                else
                    mutate("Remove animated material value", [&](auto& source) {
                        source.erase_material_parameter(binding.slot, binding.key);
                    });
            }
            FORGE_UI_PROBE("animation:material_property:" + material_slot_ + ":" + key);
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    void property_controls(Scene& scene) {
        const bool expanded = ImGui::CollapsingHeader("Animated properties");
        FORGE_UI_PROBE("animation:properties");
        if (!expanded)
            return;
        ImGui::BeginDisabled(bool(drag_key_) || curve_drag_ || box_ || pan_);
        ImGui::TextWrapped("Camera, light, audio, particle appearance and sprite frames use the "
                           "same keys. Adding a "
                           "property captures its current authored value into every key. "
                           "Key fields below set different values over time.");
        const auto entity = target_.entity && target_.scene == scene.asset_id()
                                ? scene.entity(target_.entity.str())
                                : flecs::entity{};
        for (const auto& binding : animation_property_bindings()) {
            ui::IdScope id(binding.name);
            const bool selected =
                (clip_->properties & animation_property_bit(binding.property)) != 0;
            bool enabled = selected;
            const bool available = AnimationPropertyState::available(entity, binding.property);
            ImGui::BeginDisabled(!selected && !available);
            if (ImGui::Checkbox(binding.label, &enabled)) {
                if (enabled)
                    mutate("Add animated property", [&](auto& source) {
                        const auto values = AnimationPropertyState::from(
                            entity, animation_property_bit(binding.property));
                        source.add_property(binding.property, values.value(binding.property));
                    });
                else
                    mutate("Remove animated property",
                           [&](auto& source) { source.erase_property(binding.property); });
            }
            FORGE_UI_PROBE(std::string("animation:property:") + binding.name);
            ImGui::EndDisabled();
        }
        if (!entity)
            ImGui::TextDisabled("Choose a preview object to add its camera, light, audio, particle "
                                "or sprite properties.");
        ImGui::EndDisabled();
    }
    void cancel_ruler_capture() {
        if (box_) {
            selected_keys_ = box_before_;
            key_ = box_active_;
            time_ = box_time_;
            preview_ = box_preview_;
            playing_ = box_playing_;
        }
        if (pan_)
            view_ = pan_before_;
        box_ = pan_ = false;
        box_before_.clear();
    }
    void view_controls() {
        view_.reconcile(double(clip_->duration));
        ImGui::BeginDisabled(bool(drag_key_) || curve_drag_ || box_ || pan_);
        if (ui::button("Fit clip", "Show the entire clip without changing its source."))
            view_.fit_clip();
        ImGui::SameLine();
        ImGui::BeginDisabled(selected_keys_.empty());
        if (ui::button("Fit selected keys", "Frame selected times with a small margin.")) {
            double low = clip_->duration, high = 0;
            for (const auto& key : clip_->keys)
                if (selected_keys_.contains(key.id)) {
                    low = std::min(low, double(key.time));
                    high = std::max(high, double(key.time));
                }
            view_.fit(low, high);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Zoom in", "Zoom into the center of the visible time range."))
            view_.zoom(2, .5);
        ImGui::SameLine();
        if (ui::button("Zoom out", "Zoom out around the visible time range's center."))
            view_.zoom(.5, .5);
        ImGui::EndDisabled();
        ui::help("Ctrl-wheel zooms around the pointer. Middle-drag pans time. Shift-drag selects "
                 "key diamonds; Ctrl adds and Alt removes. Escape cancels a held gesture.");
    }
    void ruler() {
        const auto start = ImGui::GetCursorScreenPos();
        std::vector<unsigned> rows;
        for (unsigned i = 0; i < 3; ++i)
            if (clip_->channels & (1u << i))
                rows.push_back(i);
        for (unsigned i = 0; i < animation_property_count; ++i)
            if (clip_->properties & (1u << i))
                rows.push_back(i + 3);
        for (unsigned i = 0; i < clip_->material_bindings.size(); ++i)
            rows.push_back(i + 3 + animation_property_count);
        const float row_height = clip_->properties || !clip_->material_bindings.empty()
                                     ? std::max(22.f, ImGui::GetTextLineHeightWithSpacing())
                                     : 22.f;
        const float width = std::max(140.f, ImGui::GetContentRegionAvail().x),
                    height = std::max(100.f, 36.f + float(rows.size()) * row_height);
        ImGui::InvisibleButton("Animation ruler", {width, height},
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonMiddle);
        FORGE_UI_PROBE("animation:ruler");
        const bool hovered = ImGui::IsItemHovered();
        const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (hovered && !popup && ImGui::GetIO().KeyCtrl)
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if ((box_ || pan_) && (start.x != ruler_origin_.x || start.y != ruler_origin_.y ||
                               width != ruler_extent_.x || height != ruler_extent_.y))
            cancel_ruler_capture();
        if (drag_key_ && (start.x != ruler_origin_.x || start.y != ruler_origin_.y ||
                          width != ruler_extent_.x || height != ruler_extent_.y ||
                          view_.first != pan_before_.first || view_.span != pan_before_.span))
            cancel_key_drag();
        const auto mouse = ImGui::GetIO().MousePos;
        const bool valid_mouse =
            ImGui::IsMousePosValid(&mouse) && std::isfinite(mouse.x) && std::isfinite(mouse.y);
        if ((box_ || pan_) && !valid_mouse)
            cancel_ruler_capture();
        if (drag_key_ && !valid_mouse)
            cancel_key_drag();
        if ((box_ && !ImGui::IsMouseDown(0) && !ImGui::IsMouseReleased(0)) ||
            (pan_ && !ImGui::IsMouseDown(2) && !ImGui::IsMouseReleased(2)))
            cancel_ruler_capture();
        if (hovered && !popup && !drag_key_ && !curve_drag_ && !box_ && !pan_ && valid_mouse) {
            if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0)
                view_.zoom(
                    std::pow(2., std::clamp(double(ImGui::GetIO().MouseWheel), -8., 8.) * .5),
                    double(mouse.x - start.x) / width);
            if (ImGui::IsMouseClicked(2)) {
                pan_ = true;
                pan_before_ = view_;
                ruler_pointer_ = mouse;
                ruler_origin_ = start;
                ruler_extent_ = {width, height};
                ruler_revision_ = document_->revision();
            }
        }
        if (pan_ && (ImGui::IsMouseDown(2) || ImGui::IsMouseReleased(2))) {
            view_ = pan_before_;
            view_.pan(-(double(mouse.x) - ruler_pointer_.x) / width * view_.span);
            if (ImGui::IsMouseReleased(2))
                pan_ = false;
        }
        const auto x = [&](double time) { return start.x + float(view_.fraction(time)) * width; };
        if (hovered && !popup && !pan_ && ImGui::IsMouseClicked(0) && valid_mouse) {
            cancel_curve_drag();
            if (!apply_pending_curves())
                return;
            if (ImGui::GetIO().KeyShift) {
                box_ = true;
                box_updated_ = false;
                box_mode_ = ImGui::GetIO().KeyAlt ? -1 : ImGui::GetIO().KeyCtrl ? 1 : 0;
                ruler_origin_ = start;
                ruler_extent_ = {width, height};
                ruler_pointer_ = mouse;
                ruler_revision_ = document_->revision();
                box_before_ = selected_keys_;
                box_active_ = key_;
                box_time_ = time_;
                box_preview_ = preview_;
                box_playing_ = playing_;
                playing_ = false;
            } else {
                AssetId selected;
                for (const auto& key : clip_->keys)
                    if (view_.visible(double(key.time)) &&
                        std::abs(x(double(key.time)) - mouse.x) < 7) {
                        selected = key.id;
                        break;
                    }
                if (selected) {
                    if (ImGui::GetIO().KeyCtrl)
                        toggle_key(selected);
                    else {
                        if (!selected_keys_.contains(selected))
                            select_key(selected);
                        else
                            activate_key(selected);
                        drag_key_ = selected;
                        ruler_origin_ = start;
                        ruler_extent_ = {width, height};
                        pan_before_ = view_;
                        drag_origin_ = std::int64_t(time_);
                        drag_pointer_x_ = mouse.x;
                        drag_moved_ = false;
                        drag_offset_ = 0;
                        drag_revision_ = document_->revision();
                        drag_keys_.clear();
                        drag_min_ = clip_->duration;
                        drag_max_ = 0;
                        for (const auto& key : clip_->keys)
                            if (selected_keys_.contains(key.id)) {
                                drag_keys_.push_back(key.id);
                                drag_min_ = std::min(drag_min_, key.time);
                                drag_max_ = std::max(drag_max_, key.time);
                            }
                    }
                } else {
                    key_ = {};
                    selected_keys_.clear();
                    time_ = view_.time(double(mouse.x - start.x) / width);
                    sample_fields(time_);
                }
                preview_ = bool(target_.entity);
                playing_ = false;
            }
        }
        ImVec2 box_low{}, box_high{};
        if (box_) {
            const ImVec2 current{std::clamp(mouse.x, start.x, start.x + width),
                                 std::clamp(mouse.y, start.y, start.y + height)};
            box_low = {std::min(current.x, ruler_pointer_.x),
                       std::min(current.y, ruler_pointer_.y)};
            box_high = {std::max(current.x, ruler_pointer_.x),
                        std::max(current.y, ruler_pointer_.y)};
            if (!box_updated_ || current.x != box_last_.x || current.y != box_last_.y) {
                selected_keys_ = box_mode_ == 0 ? std::set<AssetId>{} : box_before_;
                for (const auto& key : clip_->keys) {
                    if (!view_.visible(double(key.time)))
                        continue;
                    const auto at = x(double(key.time));
                    bool hit = false;
                    for (unsigned row = 0; row < rows.size(); ++row) {
                        const float y = start.y + 36 + row * row_height;
                        hit |= at + 5 >= box_low.x && at - 5 <= box_high.x && y + 5 >= box_low.y &&
                               y - 5 <= box_high.y;
                    }
                    if (hit) {
                        if (box_mode_ < 0)
                            selected_keys_.erase(key.id);
                        else
                            selected_keys_.insert(key.id);
                    }
                }
                box_last_ = current;
                box_updated_ = true;
            }
        }
        if (drag_key_ && ImGui::IsKeyPressed(ImGuiKey_Escape))
            cancel_key_drag();
        if (drag_key_ && valid_mouse && (ImGui::IsMouseDown(0) || ImGui::IsMouseReleased(0))) {
            drag_moved_ |= ImGui::IsMouseDragging(0) ||
                           std::abs(mouse.x - drag_pointer_x_) >= ImGui::GetIO().MouseDragThreshold;
            if (drag_moved_) {
                const auto wanted =
                    std::int64_t(std::llround(
                        std::clamp((double(mouse.x) - drag_pointer_x_) / width * view_.span,
                                   -double(clip_->duration), double(clip_->duration)) /
                        1000)) *
                    1000;
                drag_offset_ = std::clamp(wanted, -drag_min_, clip_->duration - drag_max_);
                time_ = double(drag_origin_ + drag_offset_);
            }
        }
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(start, {start.x + width, start.y + height}, true);
        draw->AddRectFilled(start, {start.x + width, start.y + height}, IM_COL32(19, 27, 36, 255));
        const int ticks =
            std::clamp(int(width / (ImGui::CalcTextSize("3600.000000").x + 16)), 1, 10);
        for (int i = 0; i <= ticks; ++i) {
            const float at = start.x + width * i / ticks;
            draw->AddLine({at, start.y}, {at, start.y + height}, IM_COL32(50, 63, 75, 255));
            char text[32];
            std::snprintf(text, sizeof(text), "%.6f",
                          view_.time(double(i) / ticks) / animation_timebase);
            draw->AddText({at + 2, start.y + 2}, IM_COL32(180, 190, 200, 255), text);
        }
        for (unsigned row = 0; row < rows.size(); ++row) {
            const auto index = rows[row];
            if (index < 3)
                continue;
            const auto label =
                index < 3 + animation_property_count
                    ? std::string(animation_property_binding(AnimationProperty(index - 3)).label)
                    : clip_->material_bindings[index - 3 - animation_property_count].slot + " / " +
                          material_field_label(
                              clip_->material_bindings[index - 3 - animation_property_count].key);
            draw->AddText(
                {start.x + 3, start.y + 36 + row * row_height - ImGui::GetTextLineHeight() / 2},
                IM_COL32(165, 177, 188, 155), label.c_str());
        }
        for (const auto& key : clip_->keys) {
            const auto shown_time =
                double(key.time) +
                (drag_key_ && selected_keys_.contains(key.id) ? double(drag_offset_) : 0.);
            if (!view_.visible(shown_time))
                continue;
            const float at = x(shown_time);
            for (unsigned row = 0; row < rows.size(); ++row) {
                const float y = start.y + 36 + row * row_height;
                const auto color = selected_keys_.contains(key.id) ? IM_COL32(255, 203, 78, 255)
                                                                   : IM_COL32(90, 177, 229, 255);
                draw->AddQuadFilled({at, y - 5}, {at + 5, y}, {at, y + 5}, {at - 5, y}, color);
            }
        }
        if (view_.visible(time_))
            draw->AddLine({x(time_), start.y}, {x(time_), start.y + height},
                          IM_COL32(110, 235, 190, 255), 2);
        if (box_) {
            draw->AddRectFilled(box_low, box_high, IM_COL32(90, 177, 229, 35));
            draw->AddRect(box_low, box_high, IM_COL32(90, 177, 229, 255));
        }
        draw->PopClipRect();
        if (box_ && ImGui::IsMouseReleased(0)) {
            box_ = false;
            box_before_.clear();
            if (!selected_keys_.contains(key_))
                key_ = {};
            for (const auto& key : clip_->keys)
                if (!key_ && selected_keys_.contains(key.id)) {
                    key_ = key.id;
                    break;
                }
            if (key_)
                activate_key(key_);
            else
                sample_fields(time_);
            preview_ = bool(target_.entity);
            playing_ = false;
        }
        if (drag_key_ && ImGui::IsMouseReleased(0)) {
            const auto keys = drag_keys_;
            const auto offset = drag_offset_;
            const auto active = drag_key_;
            cancel_key_drag();
            mutate("Move animation keys", [&](auto& source) { source.move_keys(keys, offset); });
            if (selected_keys_.contains(active))
                activate_key(active);
        }
    }
    static std::int64_t signed_offset(double seconds) {
        if (!std::isfinite(seconds) || seconds < -3600 || seconds > 3600)
            throw std::runtime_error("Key offset must be within -3600 to 3600 seconds");
        return std::llround(seconds * animation_timebase);
    }
    void batch_key_controls() {
        ImGui::SeparatorText("Key selection");
        ImGui::Text("%zu selected keys", selected_keys_.size());
        ImGui::BeginDisabled(bool(drag_key_));
        if (ui::button("Select all keys", "Select every key in this animation.")) {
            cancel_curve_drag();
            if (apply_pending_curves()) {
                selected_keys_.clear();
                for (const auto& key : clip_->keys)
                    selected_keys_.insert(key.id);
                if (!key_)
                    activate_key(clip_->keys.front().id);
            }
        }
        ImGui::SameLine();
        if (ui::button("Clear key selection", "Clear the timeline key selection.")) {
            cancel_curve_drag();
            if (apply_pending_curves()) {
                selected_keys_.clear();
                key_ = {};
            }
        }
        ImGui::InputDouble("Key offset (seconds)", &key_offset_seconds_, .1, 1, "%.3f");
        FORGE_UI_PROBE("animation:key_offset");
        ImGui::BeginDisabled(selected_keys_.empty());
        ImGui::InputInt("Frame grid (FPS)", &frame_grid_fps_);
        FORGE_UI_PROBE("animation:frame_grid");
        ui::help("Whole 1–240 frames per second, independent of playback speed. Snapping changes "
                 "selected key times, keeps slopes and rejects collisions or out-of-range keys.");
        if (ui::button("Snap selected keys to frames", "Place selected keys on the nearest frame "
                                                       "boundaries as one Undo step.")) {
            const std::vector<AssetId> keys(selected_keys_.begin(), selected_keys_.end());
            if (mutate("Snap animation keys to frames",
                       [&](auto& source) {
                           source.snap_keys_to_frames(keys, unsigned(frame_grid_fps_));
                       }) &&
                key_)
                activate_key(key_);
        }
        FORGE_UI_PROBE("animation:snap_frames");
        if (ui::button("Move selected keys",
                       "Translate the selected keys together as one Undo step.")) {
            const std::vector<AssetId> keys(selected_keys_.begin(), selected_keys_.end());
            const bool moved = mutate("Move animation keys", [&](auto& source) {
                source.move_keys(keys, signed_offset(key_offset_seconds_));
            });
            if (moved && key_)
                activate_key(key_);
        }
        if (ui::button("Duplicate selected keys",
                       "Copy the selected keys at the offset using new identities.")) {
            const std::vector<AssetId> keys(selected_keys_.begin(), selected_keys_.end());
            std::vector<AssetId> copies;
            const auto revision = document_->revision();
            mutate("Duplicate animation keys", [&](auto& source) {
                copies = source.duplicate_keys(keys, signed_offset(key_offset_seconds_));
            });
            if (document_->revision() != revision) {
                selected_keys_ = std::set<AssetId>(copies.begin(), copies.end());
                activate_key(copies.front());
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ui::help("Ctrl-click keys to add or remove them. Drag a selected timeline key to move the "
                 "group; Escape cancels. Pose and curve fields edit the active key.");
        ImGui::SeparatorText("Selected key values");
        ImGui::BeginDisabled(bool(drag_key_) || curve_drag_ || box_ || pan_);
        ImGui::InputScalarN("Position multiplier", ImGuiDataType_Double, value_position_multiplier_,
                            3, nullptr, nullptr, "%.3f");
        FORGE_UI_PROBE("animation:value_position_multiplier");
        ImGui::InputScalarN("Position offset", ImGuiDataType_Double, value_position_offset_, 3,
                            nullptr, nullptr, "%.3f");
        FORGE_UI_PROBE("animation:value_position_offset");
        ImGui::InputScalarN("Scale multiplier", ImGuiDataType_Double, value_scale_multiplier_, 3,
                            nullptr, nullptr, "%.3f");
        FORGE_UI_PROBE("animation:value_scale_multiplier");
        ImGui::BeginDisabled(selected_keys_.empty());
        if (ui::button("Apply selected key values",
                       "Multiply local positions then add the offset; multiply local scales. "
                       "Rescale matching slopes, keep times/rotation and use one Undo step.")) {
            const std::vector<AssetId> keys(selected_keys_.begin(), selected_keys_.end());
            if (mutate("Transform animation key values",
                       [&](auto& source) {
                           source.transform_key_values(
                               keys,
                               {value_position_multiplier_[0], value_position_multiplier_[1],
                                value_position_multiplier_[2]},
                               {value_position_offset_[0], value_position_offset_[1],
                                value_position_offset_[2]},
                               {value_scale_multiplier_[0], value_scale_multiplier_[1],
                                value_scale_multiplier_[2]});
                       }) &&
                key_)
                activate_key(key_);
        }
        FORGE_UI_PROBE("animation:transform_values");
        ImGui::EndDisabled();
        if (ui::button("Reset value adjustments",
                       "Reset these temporary fields; keep authored keys."))
            for (unsigned axis = 0; axis < 3; ++axis) {
                value_position_multiplier_[axis] = value_scale_multiplier_[axis] = 1;
                value_position_offset_[axis] = 0;
            }
        FORGE_UI_PROBE("animation:reset_values");
        ImGui::EndDisabled();
        ui::help("XYZ values use the local channel origin. Negative multipliers reflect a "
                 "channel; zero makes it constant. Selected-only changes affect neighboring "
                 "transitions. Channel playback masks remain; invalid curves reject atomically.");
    }
    void key_controls(Scene& scene) {
        double seconds = time_ / animation_timebase;
        if (ImGui::InputDouble("Key time (seconds)", &seconds, .1, 1, "%.3f",
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
            try {
                time_ = double(microseconds(seconds));
                if (time_ > clip_->duration)
                    time_ = clip_->duration;
            } catch (const std::exception& e) {
                report("time", e.what());
            }
        }
        FORGE_UI_PROBE("animation:key_time");
        ImGui::InputScalarN("Position", ImGuiDataType_Double, position_, 3, nullptr, nullptr,
                            "%.3f");
        FORGE_UI_PROBE("animation:position");
        ImGui::InputFloat3("Rotation (degrees)", rotation_);
        ImGui::InputFloat3("Scale", scale_);
        for (const auto& binding : animation_property_bindings()) {
            if (!(clip_->properties & animation_property_bit(binding.property)))
                continue;
            ui::IdScope id(binding.name);
            ImGui::TextWrapped("%s", binding.label);
            auto value = property_fields_[unsigned(binding.property)] * binding.display_multiplier;
            if (animation_property_discrete(binding.property)) {
                auto frame = std::uint32_t(property_fields_[unsigned(binding.property)]);
                if (ImGui::InputScalar("##value", ImGuiDataType_U32, &frame))
                    property_fields_[unsigned(binding.property)] = frame;
                ui::help("Hold this whole frame until the next key. Sprite frames have no curve "
                         "tangents.");
            } else if (ImGui::InputDouble("##value", &value, 0, 0, "%.6f"))
                property_fields_[unsigned(binding.property)] = value / binding.display_multiplier;
            FORGE_UI_PROBE(std::string("animation:value:") + binding.name);
            if (binding.property == AnimationProperty::SpriteFrame && sprite_frames) {
                const auto actor = target_.entity && target_.scene == scene.asset_id()
                                       ? scene.entity(target_.entity.str())
                                       : flecs::entity{};
                if (actor && actor.has<SpriteRenderer>()) {
                    const auto owner = "timeline:" + path_text(document_->project()) + "/" +
                                       path_text(document_->locator()) + "/" +
                                       std::to_string(document_->revision()) + "/" +
                                       target_.scene.str() + "/" + target_.entity.str();
                    auto frame = std::uint32_t(property_fields_[unsigned(binding.property)]);
                    if (sprite_frames(ui::sprite_frame_context(owner, actor.get<SpriteRenderer>()),
                                      frame))
                        property_fields_[unsigned(binding.property)] = frame;
                    ui::help("Choose a pending key value. Apply key commits it to the clip.");
                } else {
                    ImGui::TextDisabled("Choose a Sprite target to see its image frames.");
                }
            }
        }
        ImGui::Checkbox("Hold until next key", &held_);
        for (std::size_t i = 0; i < clip_->material_bindings.size(); ++i) {
            const auto& binding = clip_->material_bindings[i];
            ui::IdScope slot(binding.slot.c_str());
            ui::IdScope key(binding.key.c_str());
            ImGui::TextWrapped("%s / %s", binding.slot.c_str(),
                               material_field_label(binding.key).c_str());
            const bool color = binding.type == MaterialParameterType::LinearColor3 ||
                               binding.type == MaterialParameterType::LinearColor4;
            ImGui::InputScalarN(color ? "Linear color" : "Value", ImGuiDataType_Double,
                                material_fields_.at(i).data(),
                                material_parameter_width(binding.type), nullptr, nullptr, "%.6f");
            FORGE_UI_PROBE("animation:material_value:" + binding.slot + ":" + binding.key);
        }
        ImGui::BeginDisabled(!target_.entity ||
                             (!clip_->material_bindings.empty() && !material_ready()));
        if (ui::button("Capture authored pose", "Copy the chosen object's authored transform and "
                                                "selected properties into the key fields.")) {
            try {
                capture(scene);
            } catch (const std::exception& e) {
                report("capture", e.what());
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button(
                "Add / Update key at playhead",
                "Set these values at the playhead, preserving any existing key at that time.")) {
            const auto revision = document_->revision();
            AssetId added;
            std::vector<AnimationMaterialCurveEdit> material_edits;
            std::optional<TransformAnimationCurves> changed_curves;
            if (curve_dirty_)
                for (const auto& key : clip_->keys)
                    if (key.id == key_) {
                        material_edits = material_curve_edits(key);
                        if (!same_curves(key.curves, curves_))
                            changed_curves = curves_;
                        break;
                    }
            mutate("Set animation key", [&](auto& source) {
                const LocalTransform pose{
                    {position_[0], position_[1], position_[2]},
                    rotation_from_euler({rotation_[0], rotation_[1], rotation_[2]}),
                    checked_local_scale({scale_[0], scale_[1], scale_[2]})};
                added = source.set_key(std::llround(time_), pose, held_, {}, changed_curves,
                                       property_fields_,
                                       std::span<const AnimationMaterialValues>(material_fields_));
                if (!material_edits.empty())
                    source.set_material_curves(added, material_edits);
            });
            if (document_->revision() != revision)
                select_key(added);
        }
        ImGui::BeginDisabled(selected_keys_.empty() ||
                             selected_keys_.size() == clip_->keys.size() || bool(drag_key_));
        if (ui::button(selected_keys_.size() > 1 ? "Delete selected keys" : "Delete selected key",
                       "Remove the selection as one Undo step; retain at least one key.")) {
            const std::vector<AssetId> keys(selected_keys_.begin(), selected_keys_.end());
            mutate("Delete animation keys", [&](auto& source) { source.erase_keys(keys); });
        }
        FORGE_UI_PROBE("animation:delete_keys");
        ImGui::EndDisabled();
        if (ImGui::BeginListBox("Keys")) {
            AssetId clicked;
            bool toggle = false;
            for (const auto& key : clip_->keys) {
                const bool property_cubic =
                    std::any_of(animation_property_bindings().begin(),
                                animation_property_bindings().end(), [&](const auto& binding) {
                                    return (key.curves.property_mask &
                                            animation_property_bit(binding.property)) &&
                                           key.curves.properties[unsigned(binding.property)].cubic;
                                });
                const bool material_cubic =
                    std::any_of(key.materials.begin(), key.materials.end(), [](const auto& value) {
                        return std::any_of(value.curves.begin(), value.curves.end(),
                                           [](const auto& curve) { return curve.cubic; });
                    });
                char label[64];
                std::snprintf(label, sizeof(label), "%.3f s — %s",
                              double(key.time) / animation_timebase,
                              key.held           ? "Held"
                              : material_cubic   ? "Cubic materials"
                              : property_cubic   ? "Cubic properties"
                              : key.curves.cubic ? "Cubic vectors"
                                                 : "Linear");
                ImGui::PushID(key.id.str().c_str());
                if (ImGui::Selectable(label, selected_keys_.contains(key.id))) {
                    clicked = key.id;
                    toggle = ImGui::GetIO().KeyCtrl;
                }
                FORGE_UI_PROBE(std::string("animation:key:") + key.id.str());
                ImGui::PopID();
            }
            ImGui::EndListBox();
            // Applying pending fields can replace clip_; finish drawing its rows first.
            if (clicked) {
                if (toggle)
                    toggle_key(clicked);
                else
                    select_key(clicked);
            }
        }
        batch_key_controls();
        curve_editor();
    }

    void curve_editor() {
        const auto expanded = ImGui::CollapsingHeader("Animation curves");
        FORGE_UI_PROBE("animation:curve_section");
        if (!expanded) {
            cancel_curve_drag();
            return;
        }
        const auto selected = std::find_if(clip_->keys.begin(), clip_->keys.end(),
                                           [&](const auto& key) { return key.id == key_; });
        if (selected == clip_->keys.end()) {
            ImGui::TextUnformatted("Select a key to edit its incoming and outgoing slopes.");
            return;
        }
        const auto key = *selected;
        const bool missing_material =
            material_curve() &&
            std::none_of(clip_->material_bindings.begin(), clip_->material_bindings.end(),
                         [&](const auto& value) {
                             return value.slot == curve_material_slot_ &&
                                    value.key == curve_material_key_;
                         });
        if (curve_channel_ > material_curve_channel || missing_material ||
            (scalar_curve() && !(clip_->properties & animation_property_bit(curve_property())))) {
            cancel_curve_drag();
            curve_channel_ = 0;
        }
        const char* channels[] = {"Position", "Scale"};
        const auto curve_label =
            material_curve()
                ? curve_material_slot_ + " / " + material_field_label(curve_material_key_)
            : scalar_curve() ? std::string(animation_property_binding(curve_property()).label)
                             : std::string(channels[curve_channel_]);
        if (ImGui::BeginCombo("Curve value", curve_label.c_str())) {
            for (int channel = 0; channel < int(animation_property_count) + 2; ++channel) {
                if (channel >= 2 && (!(clip_->properties &
                                       animation_property_bit(AnimationProperty(channel - 2))) ||
                                     animation_property_discrete(AnimationProperty(channel - 2))))
                    continue;
                const auto label =
                    channel < 2 ? channels[channel]
                                : animation_property_binding(AnimationProperty(channel - 2)).label;
                if (ImGui::Selectable(label, curve_channel_ == channel))
                    curve_channel_ = channel;
                FORGE_UI_PROBE("animation:curve_channel_choice:" + std::to_string(channel));
            }
            for (const auto& binding : clip_->material_bindings) {
                const auto label = binding.slot + " / " + material_field_label(binding.key);
                const bool active = material_curve() && binding.slot == curve_material_slot_ &&
                                    binding.key == curve_material_key_;
                if (ImGui::Selectable(label.c_str(), active)) {
                    cancel_curve_drag();
                    curve_channel_ = material_curve_channel;
                    curve_material_slot_ = binding.slot;
                    curve_material_key_ = binding.key;
                }
                FORGE_UI_PROBE("animation:curve_material_choice:" + binding.slot + ":" +
                               binding.key);
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation:curve_channel");
        const auto axes_count =
            material_curve()
                ? material_parameter_width(clip_->material_bindings[material_curve_index()].type)
                : 3u;
        curve_axis_ = std::clamp(curve_axis_, 0, int(axes_count) - 1);
        std::optional<TransformAnimationTangentPreset> preset;
        if (ui::button("Flat tangents", "Set zero slopes on the selected keys."))
            preset = TransformAnimationTangentPreset::Flat;
        ImGui::SameLine();
        if (ui::button("Linear tangents",
                       "Match each neighboring segment's slope on selected keys."))
            preset = TransformAnimationTangentPreset::Linear;
        ImGui::SameLine();
        if (ui::button("Smooth tangents", "Use shared, bounded slopes; flatten extrema."))
            preset = TransformAnimationTangentPreset::Smooth;
        if (preset) {
            cancel_curve_drag();
            const std::vector<AssetId> keys(selected_keys_.begin(), selected_keys_.end());
            mutate(
                "Set tangent preset",
                [&](auto& source) {
                    if (material_curve()) {
                        const auto binding = clip_->material_bindings[material_curve_index()];
                        source.set_material_tangent_preset(keys, binding.slot, binding.key,
                                                           curve_axis_, *preset);
                    } else if (scalar_curve())
                        source.set_property_tangent_preset(keys, curve_property(), *preset);
                    else
                        source.set_tangent_preset(keys, *preset);
                },
                false);
            return; // The current document edit may replace the clip/selected iterator.
        }
        const char* modes[] = {"Linear", "Cubic"};
        ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x -
                                                   ImGui::CalcTextSize("Outgoing interpolation").x -
                                                   ImGui::GetStyle().ItemInnerSpacing.x));
        if (ImGui::BeginCombo("Outgoing interpolation", modes[curve_cubic(curves_) ? 1 : 0])) {
            for (int mode = 0; mode < 2; ++mode) {
                if (ImGui::Selectable(modes[mode], curve_cubic(curves_) == (mode == 1))) {
                    if (material_curve())
                        material_curves_.at(material_curve_index()).curves.at(curve_axis_).cubic =
                            mode == 1;
                    else if (scalar_curve()) {
                        curves_.property_mask |= animation_property_bit(curve_property());
                        curves_.properties[unsigned(curve_property())].cubic = mode == 1;
                    } else
                        curves_.cubic = mode == 1;
                    curve_dirty_ = true;
                }
                FORGE_UI_PROBE("animation:curve_choice:" + std::to_string(mode));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation:curve_mode");
        ImGui::TextDisabled(
            "Slopes use displayed units per second; rotation keeps quaternion interpolation.");
        if (key.held)
            ImGui::TextWrapped("This key holds until the next key. Its outgoing curve takes effect "
                               "after Hold is cleared.");
        auto slopes = [&](const char* label, Double3& values, const char* probe) {
            if (ImGui::InputScalarN(label, ImGuiDataType_Double, values.data(), 3, nullptr, nullptr,
                                    "%.3f"))
                curve_dirty_ = true;
            FORGE_UI_PROBE(probe);
        };
        if (vector_curve()) {
            slopes("Position incoming (units/s)", curves_.incoming.translation,
                   "animation:curve_translation_in");
            slopes("Position outgoing (units/s)", curves_.outgoing.translation,
                   "animation:curve_translation_out");
            slopes("Scale incoming (units/s)", curves_.incoming.scale, "animation:curve_scale_in");
            slopes("Scale outgoing (units/s)", curves_.outgoing.scale, "animation:curve_scale_out");
        } else {
            for (bool incoming : {true, false}) {
                double value = curve_slope(incoming) * curve_display_multiplier();
                const auto* label =
                    incoming ? "Incoming slope (units/s)" : "Outgoing slope (units/s)";
                ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x -
                                                           ImGui::CalcTextSize(label).x -
                                                           ImGui::GetStyle().ItemInnerSpacing.x));
                if (ImGui::InputDouble(label, &value, 0, 0, "%.3f")) {
                    curve_slope(incoming) = value / curve_display_multiplier();
                    if (scalar_curve())
                        curves_.property_mask |= animation_property_bit(curve_property());
                    curve_dirty_ = true;
                }
                FORGE_UI_PROBE(material_curve() ? (incoming ? "animation:curve_material_in"
                                                            : "animation:curve_material_out")
                                                : (incoming ? "animation:curve_property_in"
                                                            : "animation:curve_property_out"));
            }
        }
        if (curve_dirty_)
            ImGui::TextUnformatted(
                "Pending slopes: Apply, Save, another source edit or key selection commits them.");
        else
            ImGui::TextDisabled(
                "Drag a gold tangent vertically; Escape cancels the pending change.");
        if (!scalar_curve()) {
            const auto type = material_curve()
                                  ? clip_->material_bindings[material_curve_index()].type
                                  : MaterialParameterType::Vector3;
            const bool color = type == MaterialParameterType::LinearColor3 ||
                               type == MaterialParameterType::LinearColor4;
            const std::array<const char*, 4> axes = color ? std::array{"R", "G", "B", "A"}
                                                    : axes_count == 1
                                                        ? std::array{"Value", "", "", ""}
                                                        : std::array{"X", "Y", "Z", "W"};
            if (ImGui::BeginCombo("Axis", axes[curve_axis_])) {
                for (int axis = 0; axis < int(axes_count); ++axis) {
                    if (ImGui::Selectable(axes[axis], curve_axis_ == axis))
                        curve_axis_ = axis;
                    FORGE_UI_PROBE("animation:curve_axis_choice:" + std::to_string(axis));
                }
                ImGui::EndCombo();
            }
            FORGE_UI_PROBE("animation:curve_axis");
        }
        if (curve_drag_ &&
            (curve_drag_axis_ != curve_axis_ || curve_drag_channel_ != curve_channel_ ||
             curve_drag_material_slot_ != curve_material_slot_ ||
             curve_drag_material_key_ != curve_material_key_))
            cancel_curve_drag();
        if (ui::button("Fit curve", "Fit the applied curve without editing its source."))
            curve_fit_handles_ = false;
        ImGui::SameLine();
        if (ui::button("Fit curve and handles", "Include pending tangent endpoints in this view."))
            curve_fit_handles_ = true;
        if (vector_curve() && !(clip_->channels & (curve_channel_ == 0 ? 1 : 4)))
            ImGui::TextDisabled("This value is not animated by the clip's enabled channels.");
        const auto index = std::size_t(selected - clip_->keys.begin());
        const double begin = clip_->keys.size() == 1 ? 0
                             : index                 ? double(clip_->keys[index - 1].time)
                                                     : double(key.time);
        const double end = clip_->keys.size() == 1          ? double(clip_->duration)
                           : index + 1 < clip_->keys.size() ? double(clip_->keys[index + 1].time)
                                                            : double(key.time);
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size{std::max(100.f, ImGui::GetContentRegionAvail().x),
                          180 * ui::interface_scale};
        if (curve_drag_ && (origin.x != curve_drag_origin_.x || origin.y != curve_drag_origin_.y ||
                            size.x != curve_drag_size_.x || size.y != curve_drag_size_.y))
            cancel_curve_drag();
        ImGui::Dummy(size);
        FORGE_UI_PROBE("animation:curve_canvas");
        const auto after_canvas = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                            IM_COL32(17, 24, 32, 255));
        draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
        auto component = [&](const LocalTransform& pose) {
            return curve_channel_ == 0
                       ? std::array<double, 3>{pose.translation.x, pose.translation.y,
                                               pose.translation.z}[curve_axis_]
                       : double(std::array<float, 3>{pose.scale.x, pose.scale.y,
                                                     pose.scale.z}[curve_axis_]);
        };
        std::array<double, 193> values{};
        std::array<AnimationMaterialValues, material_parameter_override_limit> material_samples{};
        auto key_value = [&](const TransformAnimationKey& entry) {
            if (material_curve())
                return entry.materials.at(material_curve_index()).values.at(curve_axis_);
            return scalar_curve()
                       ? entry.properties[unsigned(curve_property())] * curve_display_multiplier()
                       : component(entry.pose);
        };
        double minimum = key_value(key), maximum = minimum;
        for (unsigned i = 0; i < values.size(); ++i) {
            const auto sample_time = begin + (end - begin) * double(i) / (values.size() - 1);
            if (material_curve()) {
                clip_->sample_materials(
                    sample_time,
                    std::span(material_samples).first(clip_->material_bindings.size()));
                values[i] = material_samples[material_curve_index()][curve_axis_];
            } else
                values[i] =
                    scalar_curve()
                        ? clip_->sample_properties(sample_time)[unsigned(curve_property())] *
                              curve_display_multiplier()
                        : component(clip_->sample(sample_time));
            minimum = std::min(minimum, values[i]);
            maximum = std::max(maximum, values[i]);
        }
        const auto control_value = [&](double interval, double slope, bool incoming) {
            return key_value(key) + (incoming ? -1 : 1) * slope * curve_display_multiplier() *
                                        interval / (3 * animation_timebase);
        };
        if (curve_fit_handles_) {
            auto include = [&](double interval, double slope, bool incoming) {
                const auto value = control_value(interval, slope, incoming);
                if (std::isfinite(value)) {
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                }
            };
            if (index && curve_cubic(clip_->keys[index - 1]) && !clip_->keys[index - 1].held)
                include(double(key.time) - begin, curve_slope(true), true);
            if (index + 1 < clip_->keys.size() && curve_cubic(curves_) && !key.held)
                include(end - double(key.time), curve_slope(false), false);
        }
        const double padding = std::max(.1, (maximum - minimum) * .1);
        minimum -= padding;
        maximum += padding;
        if (curve_drag_) {
            minimum = curve_drag_min_;
            maximum = curve_drag_max_;
        }
        auto point = [&](double time, double value) {
            return ImVec2{origin.x + 12 +
                              float((time - begin) / std::max(1., end - begin)) * (size.x - 24),
                          origin.y + size.y - 12 -
                              float((value - minimum) / (maximum - minimum)) * (size.y - 24)};
        };
        for (unsigned i = 1; i < values.size(); ++i)
            draw->AddLine(
                point(begin + (end - begin) * double(i - 1) / (values.size() - 1), values[i - 1]),
                point(begin + (end - begin) * double(i) / (values.size() - 1), values[i]),
                IM_COL32(83, 195, 237, 255), 2);
        // Slopes are value/time derivatives. The corresponding cubic control
        // points lie one third of the neighboring interval from this key.
        auto guide = [&](double time, double slope, bool incoming) {
            const double interval = std::abs(time - key.time);
            const double control_time = double(key.time) + (incoming ? -interval : interval) / 3;
            const double value = control_value(interval, slope, incoming);
            if (!std::isfinite(value))
                return;
            // Intersect the true slope with the view boundary. Clamping only
            // Y at the original control time would draw an incorrect slope.
            const auto start_value = key_value(key);
            const auto visible_value = std::clamp(value, minimum, maximum);
            const auto fraction =
                value == start_value ? 1 : (visible_value - start_value) / (value - start_value);
            const auto handle =
                point(double(key.time) + (control_time - key.time) * fraction, visible_value);
            draw->AddLine(point(double(key.time), key_value(key)), handle,
                          IM_COL32(244, 183, 82, 255), 1);
            if (value >= minimum && value <= maximum && interval > 0) {
                const float radius = 6 * ui::interface_scale;
                draw->AddCircleFilled(handle, 4 * ui::interface_scale, IM_COL32(244, 183, 82, 255));
                ImGui::SetCursorScreenPos({handle.x - radius, handle.y - radius});
                ImGui::InvisibleButton(incoming ? "Incoming tangent handle"
                                                : "Outgoing tangent handle",
                                       {2 * radius, 2 * radius});
                FORGE_UI_PROBE(incoming ? "animation:tangent_in" : "animation:tangent_out");
                ui::help("Drag vertically to change this slope. Key time and value stay "
                         "fixed. Escape cancels.");
                if (ImGui::IsItemActivated()) {
                    curve_drag_ = true;
                    curve_drag_key_ = key.id;
                    curve_drag_revision_ = document_->revision();
                    curve_drag_axis_ = curve_axis_;
                    curve_drag_channel_ = curve_channel_;
                    curve_drag_incoming_ = incoming;
                    curve_drag_before_ = curves_;
                    curve_drag_materials_before_ = material_curves_;
                    curve_drag_material_slot_ = curve_material_slot_;
                    curve_drag_material_key_ = curve_material_key_;
                    curve_drag_prior_dirty_ = curve_dirty_;
                    curve_drag_moved_ = false;
                    curve_drag_pointer_y_ = ImGui::GetIO().MousePos.y;
                    curve_drag_origin_ = origin;
                    curve_drag_size_ = size;
                    curve_drag_slope_ = slope;
                    curve_drag_per_pixel_ =
                        (incoming ? 1 : -1) * (maximum - minimum) / std::max(1.f, size.y - 24) *
                        (3 * animation_timebase / interval) / curve_display_multiplier();
                    curve_drag_min_ = minimum;
                    curve_drag_max_ = maximum;
                }
            }
        };
        if (index && curve_cubic(clip_->keys[index - 1]) && !clip_->keys[index - 1].held)
            guide(begin, curve_slope(true), true);
        if (index + 1 < clip_->keys.size() && curve_cubic(curves_) && !key.held)
            guide(end, curve_slope(false), false);
        for (const auto i :
             {index ? index - 1 : index, index, std::min(index + 1, clip_->keys.size() - 1)}) {
            const auto& at = clip_->keys[i];
            draw->AddCircleFilled(point(double(at.time), key_value(at)), i == index ? 5 : 3,
                                  i == index ? IM_COL32(255, 203, 78, 255)
                                             : IM_COL32(120, 165, 193, 255));
        }
        draw->PopClipRect();
        ImGui::SetCursorScreenPos(after_canvas);
        ImGui::TextDisabled("Selected key and neighboring segments; blue curve is authored, gold "
                            "guides use the slope fields.");
        ImGui::Text("%.3f–%.3f s | %.3f–%.3f", begin / animation_timebase, end / animation_timebase,
                    minimum, maximum);
        if (ui::button("Apply curve settings",
                       "Apply this key's curve settings as one source Undo step.")) {
            cancel_curve_drag();
            apply_pending_curves();
            return;
        }
        if (curve_drag_) {
            const auto mouse = ImGui::GetIO().MousePos;
            if (ImGui::IsMousePosValid(&mouse) && std::isfinite(mouse.y)) {
                const double delta = double(mouse.y) - curve_drag_pointer_y_;
                curve_drag_moved_ |= std::abs(delta) >= ImGui::GetIO().MouseDragThreshold;
                if (curve_drag_moved_) {
                    curve_slope(curve_drag_incoming_) =
                        std::clamp(curve_drag_slope_ + delta * curve_drag_per_pixel_, -1e9, 1e9);
                    if (scalar_curve())
                        curves_.property_mask |= animation_property_bit(curve_property());
                    curve_dirty_ = true;
                }
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const bool moved = curve_drag_moved_;
                curve_drag_ = false;
                if (moved)
                    apply_pending_curves();
            }
        }
    }
};
} // namespace forge
