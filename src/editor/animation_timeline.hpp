#pragma once
#include "../texture_authoring.hpp"
#include "../transform_animation_authoring.hpp"
#include "../transform_animation_preview.hpp"
#include "animation_timeline_view.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <optional>
#include <set>
#include <vector>
namespace forge {
class AnimationTimelineEditor {
  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_); }
    bool pending() const { return job_ != 0; }
    bool dirty() const {
        return document_ && (document_->dirty() || curve_dirty_ || needs_publish_ || pending());
    }
    bool can_undo() const {
        return document_ && !pending() &&
               (curve_dirty_ || curve_drag_ || box_ || pan_ || document_->can_undo());
    }
    bool can_redo() const {
        return document_ && !pending() && !curve_dirty_ && !curve_drag_ && !box_ && !pan_ &&
               document_->can_redo();
    }
    const TransformAnimationDocument* document() const { return document_.get(); }
    const std::set<AssetId>& selected_keys() const { return selected_keys_; }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() {
        cancel_ruler_capture();
        cancel_key_drag();
        cancel_curve_drag();
        save_ = true;
    }
    void request_close() {
        cancel_ruler_capture();
        cancel_key_drag();
        cancel_curve_drag();
        stop_preview();
        close_ = true;
        if (!dirty())
            finish_close();
    }
    void undo() {
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
        if (can_redo()) {
            document_->redo();
            changed();
        }
    }
    bool preview_active() const { return document_ && preview_ && target_.entity && clip_; }
    void stop_preview() { preview_ = playing_ = false; }
    Json preview(Json source) {
        if (preview_active() &&
            !apply_transform_animation_preview(source, target_, *clip_, time_)) {
            stop_preview();
            error_ = "Preview target is no longer in this scene. Choose an object again.";
        }
        return source;
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
            report("open", e.what());
        }
    }
    void content(SceneDocument& project, Scene& scene, bool locked) {
        ImGui::BeginDisabled(locked || dirty());
        if (ui::button("New transform animation...",
                       "Create a reusable animation of local position, rotation and scale."))
            ImGui::OpenPopup("New transform animation");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("New transform animation", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Project file", path_, sizeof(path_));
            ui::help("Choose a new .animation.json file inside your project.");
            ImGui::BeginDisabled(locked || dirty());
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
        if (document_ && document_->project() != project.project()) {
            finish_close();
            next_source_.reset();
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
        if (!document_)
            return;
        if (target_.entity &&
            (scene.asset_id() != target_.scene || !scene.entity(target_.entity.str()))) {
            target_ = {};
            stop_preview();
        }
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
            double seconds = time_ / animation_timebase;
            if (ImGui::SliderScalar("Playhead (seconds)", ImGuiDataType_Double, &seconds, &zero_,
                                    &duration, "%.3f")) {
                cancel_curve_drag();
                if (apply_pending_curves()) {
                    time_ = seconds * animation_timebase;
                    key_ = {};
                    selected_keys_.clear();
                    fields(clip_->sample(time_));
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
            if (preview_)
                ImGui::TextColored({.4f, .85f, 1, 1},
                                   "PREVIEW — temporary view; Scene source is unchanged");
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
        target_ = {scene.asset_id(), EntityId::parse(id)};
        capture(scene);
    }

  private:
    std::unique_ptr<TransformAnimationDocument> document_;
    std::unique_ptr<AssetImportService> service_;
    std::optional<TransformAnimationClip> clip_;
    std::shared_ptr<const AssetCatalog> published_;
    std::optional<std::filesystem::path> next_source_;
    EntityRef target_{};
    AssetId key_{}, drag_key_{};
    std::set<AssetId> selected_keys_;
    std::vector<AssetId> drag_keys_;
    std::int64_t drag_origin_ = 0, drag_offset_ = 0, drag_min_ = 0, drag_max_ = 0;
    std::uint64_t drag_revision_ = 0;
    float drag_pointer_x_ = 0;
    bool drag_moved_ = false;
    double key_offset_seconds_ = 0;
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
        box_ = pan_ = false;
        box_before_.clear();
        view_ = {};
        curve_drag_ = false;
        curve_fit_handles_ = false;
        stop_preview();
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
        if (key_) {
            const auto at = std::find_if(clip_->keys.begin(), clip_->keys.end(),
                                         [&](const auto& k) { return k.id == key_; });
            if (at == clip_->keys.end())
                key_ = {};
            else {
                held_ = at->held;
                curves_ = at->curves;
                curve_dirty_ = false;
                fields(at->pose);
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
                            source.set_key(key.time, key.pose, key.held, key.id, curves_);
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
    void fields(const LocalTransform& pose) {
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
    void restore_curve_fields() {
        if (clip_)
            for (const auto& key : clip_->keys)
                if (key.id == key_)
                    curves_ = key.curves;
        curve_dirty_ = false;
    }
    void cancel_curve_drag() {
        if (!curve_drag_)
            return;
        curves_ = curve_drag_before_;
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
                "Set vector curves",
                [&](auto& source) {
                    source.set_key(key.time, key.pose, key.held, key.id, curves_);
                },
                false))
            return false;
        curve_dirty_ = false;
        return true;
    }
    void activate_key(AssetId id) {
        for (const auto& key : clip_->keys)
            if (key.id == id) {
                key_ = id;
                time_ = double(key.time);
                held_ = key.held;
                curves_ = key.curves;
                curve_dirty_ = false;
                fields(key.pose);
                return;
            }
    }
    void capture(Scene& scene) {
        const auto entity = scene.entity(target_.entity.str());
        if (!entity || scene.asset_id() != target_.scene)
            throw std::runtime_error("Animation preview object is unavailable");
        fields(
            {entity.has<LocalTranslation>() ? entity.get<LocalTranslation>() : LocalTranslation{},
             entity.has<LocalRotation>() ? entity.get<LocalRotation>() : LocalRotation{},
             entity.has<LocalScale>() ? entity.get<LocalScale>() : LocalScale{}});
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
            ImGui::SameLine();
        }
        ImGui::NewLine();
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
        const float width = std::max(140.f, ImGui::GetContentRegionAvail().x), height = 100;
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
                    fields(clip_->sample(time_));
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
                    for (unsigned row = 0; row < 3; ++row) {
                        const float y = start.y + 36 + row * 22;
                        hit |= (clip_->channels & (1u << row)) && at + 5 >= box_low.x &&
                               at - 5 <= box_high.x && y + 5 >= box_low.y && y - 5 <= box_high.y;
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
        for (const auto& key : clip_->keys) {
            const auto shown_time =
                double(key.time) +
                (drag_key_ && selected_keys_.contains(key.id) ? double(drag_offset_) : 0.);
            if (!view_.visible(shown_time))
                continue;
            const float at = x(shown_time);
            for (unsigned row = 0; row < 3; ++row)
                if (clip_->channels & (1u << row)) {
                    const float y = start.y + 36 + row * 22;
                    const auto color = selected_keys_.contains(key.id)
                                           ? IM_COL32(255, 203, 78, 255)
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
                fields(clip_->sample(time_));
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
        ImGui::InputScalarN("Position", ImGuiDataType_Double, position_, 3, nullptr, nullptr,
                            "%.3f");
        FORGE_UI_PROBE("animation:position");
        ImGui::InputFloat3("Rotation (degrees)", rotation_);
        ImGui::InputFloat3("Scale", scale_);
        ImGui::Checkbox("Hold until next key", &held_);
        ImGui::BeginDisabled(!target_.entity);
        if (ui::button("Capture authored pose",
                       "Copy the chosen object's authored local transform into the key fields.")) {
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
            mutate("Set animation key", [&](auto& source) {
                const LocalTransform pose{
                    {position_[0], position_[1], position_[2]},
                    rotation_from_euler({rotation_[0], rotation_[1], rotation_[2]}),
                    checked_local_scale({scale_[0], scale_[1], scale_[2]})};
                added = source.set_key(
                    std::llround(time_), pose, held_, {},
                    curve_dirty_ ? std::optional<TransformAnimationCurves>(curves_) : std::nullopt);
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
                char label[64];
                std::snprintf(label, sizeof(label), "%.3f s — %s",
                              double(key.time) / animation_timebase,
                              key.held           ? "Held"
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
        const auto expanded = ImGui::CollapsingHeader("Vector curves");
        FORGE_UI_PROBE("animation:curve_section");
        if (!expanded) {
            cancel_curve_drag();
            return;
        }
        const auto selected = std::find_if(clip_->keys.begin(), clip_->keys.end(),
                                           [&](const auto& key) { return key.id == key_; });
        if (selected == clip_->keys.end()) {
            ImGui::TextUnformatted("Select a key to edit its incoming and outgoing vector slopes.");
            return;
        }
        const auto key = *selected;
        std::optional<TransformAnimationTangentPreset> preset;
        if (ui::button("Flat tangents", "Set zero vector slopes on all selected keys."))
            preset = TransformAnimationTangentPreset::Flat;
        ImGui::SameLine();
        if (ui::button("Linear tangents",
                       "Match each neighboring segment's slope on selected keys."))
            preset = TransformAnimationTangentPreset::Linear;
        ImGui::SameLine();
        if (ui::button("Smooth tangents", "Use shared, bounded slopes; flatten vector extrema."))
            preset = TransformAnimationTangentPreset::Smooth;
        if (preset) {
            cancel_curve_drag();
            const std::vector<AssetId> keys(selected_keys_.begin(), selected_keys_.end());
            mutate(
                "Set tangent preset",
                [&](auto& source) { source.set_tangent_preset(keys, *preset); }, false);
            return; // The current document edit may replace the clip/selected iterator.
        }
        const char* modes[] = {"Linear", "Cubic"};
        if (ImGui::BeginCombo("Outgoing vectors", modes[curves_.cubic ? 1 : 0])) {
            for (int mode = 0; mode < 2; ++mode) {
                if (ImGui::Selectable(modes[mode], curves_.cubic == (mode == 1))) {
                    curves_.cubic = mode == 1;
                    curve_dirty_ = true;
                }
                FORGE_UI_PROBE("animation:curve_choice:" + std::to_string(mode));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation:curve_mode");
        ImGui::TextDisabled(
            "Position and scale use these curves; rotation keeps quaternion interpolation.");
        if (key.held)
            ImGui::TextWrapped("This key holds until the next key. Its outgoing curve takes effect "
                               "after Hold is cleared.");
        auto slopes = [&](const char* label, Double3& values, const char* probe) {
            if (ImGui::InputScalarN(label, ImGuiDataType_Double, values.data(), 3, nullptr, nullptr,
                                    "%.3f"))
                curve_dirty_ = true;
            FORGE_UI_PROBE(probe);
        };
        slopes("Position incoming (units/s)", curves_.incoming.translation,
               "animation:curve_translation_in");
        slopes("Position outgoing (units/s)", curves_.outgoing.translation,
               "animation:curve_translation_out");
        slopes("Scale incoming (units/s)", curves_.incoming.scale, "animation:curve_scale_in");
        slopes("Scale outgoing (units/s)", curves_.outgoing.scale, "animation:curve_scale_out");
        if (curve_dirty_)
            ImGui::TextUnformatted(
                "Pending slopes: Apply, Save, another source edit or key selection commits them.");
        else
            ImGui::TextDisabled(
                "Drag a gold tangent vertically; Escape cancels the pending change.");
        const char* channels[] = {"Position", "Scale"};
        if (ImGui::BeginCombo("Curve value", channels[curve_channel_])) {
            for (int channel = 0; channel < 2; ++channel) {
                if (ImGui::Selectable(channels[channel], curve_channel_ == channel))
                    curve_channel_ = channel;
                FORGE_UI_PROBE("animation:curve_channel_choice:" + std::to_string(channel));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation:curve_channel");
        ImGui::SameLine();
        const char* axes[] = {"X", "Y", "Z"};
        if (ImGui::BeginCombo("Axis", axes[curve_axis_])) {
            for (int axis = 0; axis < 3; ++axis) {
                if (ImGui::Selectable(axes[axis], curve_axis_ == axis))
                    curve_axis_ = axis;
                FORGE_UI_PROBE("animation:curve_axis_choice:" + std::to_string(axis));
            }
            ImGui::EndCombo();
        }
        FORGE_UI_PROBE("animation:curve_axis");
        if (curve_drag_ &&
            (curve_drag_axis_ != curve_axis_ || curve_drag_channel_ != curve_channel_))
            cancel_curve_drag();
        if (ui::button("Fit curve", "Fit the applied curve without editing its source."))
            curve_fit_handles_ = false;
        ImGui::SameLine();
        if (ui::button("Fit curve and handles", "Include pending tangent endpoints in this view."))
            curve_fit_handles_ = true;
        if (!(clip_->channels & (curve_channel_ == 0 ? 1 : 4)))
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
        double minimum = component(key.pose), maximum = minimum;
        for (unsigned i = 0; i < values.size(); ++i) {
            values[i] =
                component(clip_->sample(begin + (end - begin) * double(i) / (values.size() - 1)));
            minimum = std::min(minimum, values[i]);
            maximum = std::max(maximum, values[i]);
        }
        const auto control_value = [&](double interval, double slope, bool incoming) {
            return component(key.pose) +
                   (incoming ? -1 : 1) * slope * interval / (3 * animation_timebase);
        };
        if (curve_fit_handles_) {
            auto include = [&](double interval, double slope, bool incoming) {
                const auto value = control_value(interval, slope, incoming);
                if (std::isfinite(value)) {
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                }
            };
            if (index && clip_->keys[index - 1].curves.cubic && !clip_->keys[index - 1].held)
                include(double(key.time) - begin,
                        (curve_channel_ == 0 ? curves_.incoming.translation
                                             : curves_.incoming.scale)[curve_axis_],
                        true);
            if (index + 1 < clip_->keys.size() && curves_.cubic && !key.held)
                include(end - double(key.time),
                        (curve_channel_ == 0 ? curves_.outgoing.translation
                                             : curves_.outgoing.scale)[curve_axis_],
                        false);
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
            const auto start_value = component(key.pose);
            const auto visible_value = std::clamp(value, minimum, maximum);
            const auto fraction =
                value == start_value ? 1 : (visible_value - start_value) / (value - start_value);
            const auto handle =
                point(double(key.time) + (control_time - key.time) * fraction, visible_value);
            draw->AddLine(point(double(key.time), component(key.pose)), handle,
                          IM_COL32(244, 183, 82, 255), 1);
            if (value >= minimum && value <= maximum && interval > 0) {
                const float radius = 6 * ui::interface_scale;
                draw->AddCircleFilled(handle, 4 * ui::interface_scale, IM_COL32(244, 183, 82, 255));
                ImGui::SetCursorScreenPos({handle.x - radius, handle.y - radius});
                ImGui::InvisibleButton(incoming ? "Incoming tangent handle"
                                                : "Outgoing tangent handle",
                                       {2 * radius, 2 * radius});
                FORGE_UI_PROBE(incoming ? "animation:tangent_in" : "animation:tangent_out");
                ui::help("Drag vertically to change this vector slope. Key time and value stay "
                         "fixed. Escape cancels.");
                if (ImGui::IsItemActivated()) {
                    curve_drag_ = true;
                    curve_drag_key_ = key.id;
                    curve_drag_revision_ = document_->revision();
                    curve_drag_axis_ = curve_axis_;
                    curve_drag_channel_ = curve_channel_;
                    curve_drag_incoming_ = incoming;
                    curve_drag_before_ = curves_;
                    curve_drag_prior_dirty_ = curve_dirty_;
                    curve_drag_moved_ = false;
                    curve_drag_pointer_y_ = ImGui::GetIO().MousePos.y;
                    curve_drag_origin_ = origin;
                    curve_drag_size_ = size;
                    curve_drag_slope_ = slope;
                    curve_drag_per_pixel_ = (incoming ? 1 : -1) * (maximum - minimum) /
                                            std::max(1.f, size.y - 24) *
                                            (3 * animation_timebase / interval);
                    curve_drag_min_ = minimum;
                    curve_drag_max_ = maximum;
                }
            }
        };
        if (index && clip_->keys[index - 1].curves.cubic && !clip_->keys[index - 1].held)
            guide(begin,
                  (curve_channel_ == 0 ? curves_.incoming.translation
                                       : curves_.incoming.scale)[curve_axis_],
                  true);
        if (index + 1 < clip_->keys.size() && curves_.cubic && !key.held)
            guide(end,
                  (curve_channel_ == 0 ? curves_.outgoing.translation
                                       : curves_.outgoing.scale)[curve_axis_],
                  false);
        for (const auto i :
             {index ? index - 1 : index, index, std::min(index + 1, clip_->keys.size() - 1)}) {
            const auto& at = clip_->keys[i];
            draw->AddCircleFilled(point(double(at.time), component(at.pose)), i == index ? 5 : 3,
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
                       "Apply only this key's vector curve mode/slopes as one source Undo step.")) {
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
                    auto& side = curve_drag_incoming_ ? curves_.incoming : curves_.outgoing;
                    auto& values = curve_drag_channel_ == 0 ? side.translation : side.scale;
                    values[curve_drag_axis_] =
                        std::clamp(curve_drag_slope_ + delta * curve_drag_per_pixel_, -1e9, 1e9);
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
