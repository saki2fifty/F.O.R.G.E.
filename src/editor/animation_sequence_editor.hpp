#pragma once
#include "../animation_material_patch.hpp"
#include "../animation_playback_access.hpp"
#include "animation_timeline_view.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include "property_drawer.hpp"
#include "sequence_frame_source.hpp"
#include "widgets.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <limits>
namespace forge {
// A borrowed Scene editing mode of the Timeline, not another document owner.
class AnimationSequenceEditor {
  public:
    std::function<void()> export_frames_requested;
    std::function<bool()> export_frames_available;
    bool save_requested = false;
    bool close_cancelled = false;
    bool is_open() const { return bool(controller_.entity); }
    bool dirty() const { return pending_ || (project_ && project_->dirty()); }
    bool pending_fields() const { return pending_; }
    bool can_undo() const {
        return scene_ && !locked_ && !frame_exporting_ && (pending_ || scene_->can_undo());
    }
    bool can_redo() const {
        return scene_ && !locked_ && !frame_exporting_ && !pending_ && scene_->can_redo();
    }
    EntityRef controller() const { return controller_; }
    void open(SceneDocument& project, Scene& scene, EntityRef controller) {
        close();
        if (controller.scene != scene.asset_id() ||
            !scene.entity(controller.entity.str()).has<AnimationSequence>())
            throw std::runtime_error("Choose an Animation Sequence controller");
        project_ = &project;
        scene_ = &scene;
        controller_ = controller;
        project_path_ = project.project();
        project_generation_ = project.generation();
        refresh();
    }
    void close() {
        stop_preview();
        context_.reset();
        catalog_.reset();
        project_ = nullptr;
        scene_ = nullptr;
        controller_ = {};
        track_.clear();
        shot_.clear();
        shot_camera_ = nullptr;
        cue_.clear();
        cue_source_ = nullptr;
        pending_ = false;
        save_requested = false;
        close_cancelled = false;
        closing_ = false;
        time_ = offset_ = 0;
        view_ = {};
    }
    void request_save() {
        if (!pending_ || apply_pending())
            save_requested = true;
    }
    void request_close() {
        if (pending_)
            closing_ = true;
        else
            close();
    }
    void close_dialog() {
        if (closing_)
            ImGui::OpenPopup("Pending sequence field");
        if (ImGui::BeginPopupModal("Pending sequence field", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Sequence fields are pending. Scene changes stay in the Scene draft "
                               "when this view closes.");
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            ImGui::BeginDisabled(locked_);
            if (ui::button("Apply and close",
                           "Commit the field to Scene history, then close this view.") &&
                apply_pending()) {
                close();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            if (ui::button("Discard field and close", "Keep the existing Scene draft value.")) {
                close();
                ImGui::CloseCurrentPopup();
            }
            if (ui::button("Keep editing", "Return to this pending field.")) {
                closing_ = false;
                close_cancelled = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    void undo() {
        if (!can_undo())
            return;
        if (pending_)
            pending_ = false;
        else
            authoring_history(*scene_, false);
        stop_preview();
    }
    void redo() {
        if (can_redo()) {
            authoring_history(*scene_, true);
            stop_preview();
        }
    }
    void stop_preview() { preview_ = playing_ = frame_exporting_ = false; }
    bool preview_active() const { return preview_ && ready_ && context_; }
    using FrameExportInfo = ui::SequenceFrameSourceInfo;
    bool frame_export_ready() const { return !frame_exporting_ && frame_source_current(); }
    FrameExportInfo frame_export_info() const {
        if (!frame_export_ready())
            throw std::runtime_error("Finish sequence fields and refresh ready published clips "
                                     "before exporting frames");
        return {controller_,
                scene_->entity(controller_.entity.str()).get<AnimationSequence>().duration,
                revision_, frame_export_serial_, catalog_};
    }
    Json frame_export_document() const {
        if (!frame_source_current())
            throw std::runtime_error("Sequence source changed before frame export");
        return scene_->document();
    }
    std::uint64_t begin_frame_export() {
        (void)frame_export_info();
        if (frame_export_serial_ == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Sequence preview export generation exhausted");
        saved_preview_time_ = time_;
        saved_preview_ = preview_;
        saved_playing_ = playing_;
        frame_exporting_ = preview_ = true;
        playing_ = false;
        return ++frame_export_serial_;
    }
    bool frame_export_current(std::uint64_t token) const {
        return frame_exporting_ && token == frame_export_serial_ && frame_source_current();
    }
    void sample_frame_export(std::uint64_t token, double seconds) {
        if (!frame_export_current(token))
            throw std::runtime_error("Sequence preview changed during frame export");
        const auto duration =
            scene_->entity(controller_.entity.str()).get<AnimationSequence>().duration;
        // The runtime intentionally maps an exact loop endpoint to zero. Frame
        // ranges are end-exclusive, so explicit sample times never wrap silently.
        if (!std::isfinite(seconds) || seconds < 0 || seconds >= duration)
            throw std::runtime_error("Frame sample must be inside the sequence duration");
        AnimationPlaybackAccess::control(*context_->animation, controller_,
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
        time_ = seconds;
        preview_ = true;
        playing_ = false;
    }
    void end_frame_export(std::uint64_t token) {
        if (!frame_exporting_ || token != frame_export_serial_)
            return;
        const bool current = frame_export_current(token);
        frame_exporting_ = false;
        if (!current) {
            stop_preview();
            return;
        }
        try {
            AnimationPlaybackAccess::control(
                *context_->animation, controller_,
                {{"operation", "seek"}, {"position_seconds", saved_preview_time_}});
            time_ = saved_preview_time_;
            preview_ = saved_preview_;
            playing_ = saved_playing_;
        } catch (...) {
            stop_preview();
            throw;
        }
    }
    void asset_catalog_changed(const std::shared_ptr<const AssetCatalog>& catalog) {
        if (context_ && catalog_ && catalog && catalog_->document() != catalog->document()) {
            stale_ = true;
            stop_preview();
        }
    }
    void poll(bool locked, double seconds) {
        locked_ = locked;
        if (!is_open())
            return;
        if (scene_->asset_id() != controller_.scene || project_->project() != project_path_ ||
            project_->generation() != project_generation_) {
            close();
            close_cancelled = true; // Cancel queued mode switches into the retired context.
            return;
        }
        if (locked) {
            stop_preview();
            return;
        }
        if (scene_->revision() != revision_) {
            stale_ = true;
            stop_preview();
        }
        try {
            if (context_ && !ready_ && !stale_) {
                context_->engine.services().resources()->synchronize();
                ready_ = context_->animation->prepare_initial_pose();
                if (ready_)
                    seek_preview();
            }
            if (playing_ && preview_active()) {
                const auto config =
                    scene_->entity(controller_.entity.str()).get<AnimationSequence>();
                time_ += std::clamp(seconds, 0., .25) * config.playback_speed;
                if (config.loop)
                    time_ = std::fmod(time_, config.duration);
                else if (time_ >= config.duration) {
                    time_ = config.duration;
                    playing_ = false;
                }
                seek_preview();
            }
        } catch (const std::exception& e) {
            error_ = e.what();
            stop_preview();
            context_.reset();
        }
    }
    Json preview(Json source) const {
        if (!preview_active())
            return source;
        for (auto& row : source.at("entities")) {
            const auto id = row.at("id").get<std::string>();
            const auto actor = context_->scene.entity(id);
            if (targets_.contains(id))
                for (const auto& type : detail::builtins()) {
                    const std::string_view name = type.name;
                    if (name != "forge.local_translation" && name != "forge.local_rotation" &&
                        name != "forge.local_scale" && name != "forge.camera" &&
                        name != "forge.light" && name != "forge.audio_source" &&
                        name != "forge.particle_emitter" && name != "forge.mesh_renderer")
                        continue;
                    const auto fields = type.read(actor, true);
                    if (fields.is_null())
                        continue;
                    if (name == "forge.mesh_renderer") {
                        merge_animation_material_parameters(row["components"][type.name],
                                                            fields.at("material_parameters"));
                        continue;
                    }
                    for (const auto& [field, value] : fields.items())
                        row["components"][type.name][field] = value;
                }
            const auto patch = context_->animation->presentation_properties(actor.id(), 1);
            if (patch.contains("forge.camera") && patch.at("forge.camera").contains("enabled"))
                row["components"]["forge.camera"]["enabled"] =
                    patch.at("forge.camera").at("enabled");
        }
        return source;
    }
    void draw(bool locked) {
        if (!is_open())
            return;
        const auto entity = scene_->entity(controller_.entity.str());
        if (!entity || !entity.has<AnimationSequence>()) {
            ImGui::TextWrapped("Sequence controller is missing. Scene Undo can restore it.");
            return;
        }
        const auto config = entity.get<AnimationSequence>();
        view_.reconcile(config.duration * animation_timebase);
        ImGui::TextUnformatted("Scene sequence — arrangement edits use Scene Save and Undo");
        ImGui::BeginDisabled(locked || frame_exporting_);
        if (ui::button("Save scene", "Save this arrangement with its Scene."))
            request_save();
        if (ui::button("Select controller", "Edit duration, loop and speed in Inspector.") &&
            ui::editor_context)
            ui::editor_context->selection.select_entity(controller_.entity.str());
        ImGui::TextWrapped("Duration %.3f s | %s | Speed %.2f", config.duration,
                           config.loop ? "Loop" : "One-shot", config.playback_speed);
        if (ui::button("Refresh clips",
                       "Load published clips into the isolated animation preview."))
            refresh();
        FORGE_UI_PROBE("sequence:refresh");
        if (stale_)
            ImGui::TextWrapped(
                "Scene or published assets changed. Refresh clips before previewing again.");
        else if (context_ && !ready_)
            ImGui::TextWrapped("Preparing published clips...");
        ImGui::BeginDisabled(!ready_ || stale_ || !config.enabled);
        if (ui::button(preview_ ? "Stop preview" : "Preview sequence",
                       "Substitute a copied Scene view; authored objects stay unchanged.")) {
            preview_ = !preview_;
            if (!preview_)
                playing_ = false;
            else
                seek_preview();
        }
        FORGE_UI_PROBE("sequence:preview");
        if (ui::button(playing_ ? "Pause preview" : "Play preview",
                       "Advance this isolated sequence preview.")) {
            preview_ = true;
            playing_ = !playing_;
        }
        FORGE_UI_PROBE("sequence:play");
        ImGui::SameLine();
        ImGui::BeginDisabled(!frame_export_ready() || !export_frames_requested ||
                             (export_frames_available && !export_frames_available()));
        if (ui::button("Export frames…", "Export the ready sequence preview as numbered PNGs. "
                                         "Wait for image and source-import work to finish."))
            export_frames_requested();
        FORGE_UI_PROBE("sequence:export-frames");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        const double zero = 0;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderScalar("##sequence time", ImGuiDataType_Double, &time_, &zero,
                                &config.duration, "%.3f s", ImGuiSliderFlags_AlwaysClamp) &&
            ready_ && !stale_) {
            playing_ = false;
            seek_preview();
        }
        FORGE_UI_PROBE("sequence:playhead");
        if (ui::button("Fit sequence", "Show the whole sequence duration."))
            view_.fit_clip();
        ImGui::SameLine();
        if (ui::button("Zoom in", "Zoom around the view center."))
            view_.zoom(2, .5);
        ImGui::SameLine();
        if (ui::button("Zoom out", "Zoom around the view center."))
            view_.zoom(.5, .5);
        const auto snapshot = scene_->effective_document();
        const auto selected =
            ui::editor_context ? ui::editor_context->selection.entity() : std::string();
        const bool can_add = !pending_ && !selected.empty() &&
                             selected != controller_.entity.str() && scene_->entity(selected) &&
                             !scene_->entity(selected).has<AnimationSequence>();
        ImGui::BeginDisabled(!can_add);
        if (ui::button("Add selected object", "Bind its Transform Animator to this sequence; "
                                              "existing clip settings stay stored.")) {
            try {
                Json commands = Json::array();
                if (!scene_->entity(selected).has<TransformAnimator>())
                    commands.push_back(
                        {{"operation", "component.add"},
                         {"arguments",
                          {{"entity", selected}, {"component", "forge.transform_animator"}}}});
                commands.push_back(property(selected, "sequence", controller_));
                apply_authoring(*scene_, commands, scene_->revision());
                track_ = selected;
                shot_.clear();
                cue_.clear();
                pending_ = false;
                stop_preview();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        FORGE_UI_PROBE("sequence:add");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Object tracks");
        for (const auto& row : snapshot.at("entities")) {
            const auto id = row.at("id").get<std::string>();
            const auto actor = scene_->entity(id);
            if (!actor.has<TransformAnimator>())
                continue;
            const auto value = actor.get<TransformAnimator>();
            if (value.sequence != controller_)
                continue;
            ImGui::PushID(id.c_str());
            ImGui::BeginDisabled(pending_);
            if (ImGui::Selectable(row.value("name", id).c_str(), track_ == id)) {
                track_ = id;
                shot_.clear();
                cue_.clear();
            }
            FORGE_UI_PROBE("sequence:track:" + id);
            ImGui::EndDisabled();
            const auto origin = ImGui::GetCursorScreenPos();
            const auto size =
                ImVec2{std::max(40.f, ImGui::GetContentRegionAvail().x), 24 * ui::interface_scale};
            ImGui::InvisibleButton("##track span", size);
            FORGE_UI_PROBE("sequence:span:" + id);
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                                IM_COL32(32, 42, 52, 255));
            double duration = 0;
            if (ready_ && context_) {
                const auto status =
                    context_->animation->playback({controller_.scene, EntityId::parse(id)});
                if (status)
                    duration = status->duration;
            }
            const auto x = [&](double seconds) {
                return origin.x +
                       float(std::clamp(view_.fraction(seconds * animation_timebase), 0., 1.)) *
                           size.x;
            };
            if (duration > 0)
                draw->AddRectFilled(
                    {x(value.sequence_start_seconds), origin.y + 3},
                    {x(value.sequence_start_seconds + duration), origin.y + size.y - 3},
                    IM_COL32(55, 125, 164, 255));
            draw->AddLine({x(time_), origin.y}, {x(time_), origin.y + size.y},
                          IM_COL32(255, 203, 78, 255), 2);
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
                time_ = view_.time((ImGui::GetIO().MousePos.x - origin.x) / size.x) /
                        animation_timebase;
                playing_ = false;
                if (ready_ && !stale_)
                    seek_preview();
            }
            ImGui::TextDisabled("Start %.3f s%s", value.sequence_start_seconds,
                                value.enabled ? "" : " | Disabled");
            ImGui::PopID();
        }
        if (!track_.empty() && scene_->entity(track_) &&
            scene_->entity(track_).has<TransformAnimator>()) {
            const auto value = scene_->entity(track_).get<TransformAnimator>();
            if (value.sequence == controller_) {
                if (!pending_)
                    offset_ = value.sequence_start_seconds;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                if (ImGui::InputDouble("##track start", &offset_, 0, 0, "%.3f s")) {
                    if (!pending_)
                        pending_revision_ = scene_->revision();
                    pending_ = true;
                }
                FORGE_UI_PROBE("sequence:start");
                if (ui::button("Apply track start", "Commit this offset as one Scene Undo step."))
                    apply_pending();
                FORGE_UI_PROBE("sequence:apply_start");
                if (ui::button("Reset pending start", "Reload the saved Scene draft value.")) {
                    pending_ = false;
                }
                if (pending_)
                    ImGui::TextWrapped("Apply or reset the pending start before changing tracks.");
                ImGui::BeginDisabled(pending_);
                ImGui::SetNextItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x -
                                                           ImGui::CalcTextSize("Clip").x -
                                                           ImGui::GetStyle().ItemInnerSpacing.x));
                Json clip = value.clip.id ? Json(value.clip.id) : Json();
                if (catalog_ && asset_ref_picker(*catalog_, clip, TransformAnimationAsset::type,
                                                 "Clip", false)) {
                    try {
                        apply_authoring(*scene_, Json::array({property(track_, "clip", clip)}),
                                        scene_->revision());
                        stop_preview();
                        pending_ = false;
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                FORGE_UI_PROBE("sequence:clip");
                if (ui::button("Remove from sequence", "Keep the clip and standalone settings; "
                                                       "clear only its sequence binding.")) {
                    try {
                        apply_authoring(*scene_,
                                        Json::array({property(track_, "sequence", nullptr)}),
                                        scene_->revision());
                        track_.clear();
                        cue_.clear();
                        pending_ = false;
                        stop_preview();
                    } catch (const std::exception& e) {
                        error_ = e.what();
                    }
                }
                FORGE_UI_PROBE("sequence:remove");
                ImGui::EndDisabled();
            }
        }
        draw_camera_shots(snapshot, config, selected);
        draw_audio_cues(snapshot, config, selected);
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndDisabled();
    }

  private:
    struct Context {
        EngineContext engine;
        Scene scene;
        std::shared_ptr<AnimationRuntime> animation;
        std::set<std::string> targets;
        Context(const std::filesystem::path& project, const Json& source, EntityRef controller)
            : engine(WorldRole::Runtime, false,
                     {runtime_resources_module(project), animation_module(project)}),
              scene(engine.world()), animation(animation_runtime(engine.world())) {
            scene.restore_snapshot(source);
            // Keep movement guards on targets/ancestors so preview uses the
            // runtime's own writer-conflict policy. Unrelated animation stays idle.
            std::set<flecs::entity_t> guards;
            for (const auto& row : source.at("entities")) {
                const auto id = row.at("id").get<std::string>();
                const auto actor = scene.entity(id);
                if (!actor)
                    continue;
                if (id == controller.entity.str())
                    for (auto parent = actor; parent && guards.insert(parent.id()).second;
                         parent = parent.target(flecs::ChildOf)) {
                    }
                if (!actor.has<TransformAnimator>())
                    continue;
                const auto value = actor.get<TransformAnimator>();
                if (value.sequence != controller || !value.enabled)
                    continue;
                targets.insert(id);
                for (auto parent = actor; parent && guards.insert(parent.id()).second;
                     parent = parent.target(flecs::ChildOf)) {
                }
            }
            for (const auto& row : source.at("entities")) {
                const auto id = row.at("id").get<std::string>();
                auto entity = scene.entity(id);
                if (!entity)
                    continue;
                if (entity.has<AnimationSequence>()) {
                    auto value = entity.get<AnimationSequence>();
                    if (id == controller.entity.str())
                        value.play_on_start = false;
                    else if (!targets.contains(id))
                        value.enabled = false;
                    entity.set(value);
                }
                if (entity.has<CameraShot>()) {
                    auto value = entity.get<CameraShot>();
                    if (value.sequence != controller)
                        value.enabled = false;
                    entity.set(value);
                }
                if (entity.has<Animator>() && !guards.contains(entity.id())) {
                    auto value = entity.get<Animator>();
                    value.enabled = false;
                    entity.set(value);
                }
                if (entity.has<TransformAnimator>() && id != controller.entity.str()) {
                    auto value = entity.get<TransformAnimator>();
                    if (!targets.contains(id))
                        value.enabled = false;
                    entity.set(value);
                }
            }
        }
    };
    SceneDocument* project_ = nullptr;
    std::filesystem::path project_path_;
    std::uint64_t project_generation_ = 0;
    Scene* scene_ = nullptr;
    EntityRef controller_;
    std::unique_ptr<Context> context_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::set<std::string> targets_;
    std::string track_, shot_, cue_, error_;
    Json shot_camera_ = nullptr, cue_source_ = nullptr;
    double shot_start_ = 0, shot_end_ = 1, cue_time_ = 0;
    AnimationTimelineView view_;
    std::uint64_t revision_ = 0, pending_revision_ = 0, frame_export_serial_ = 0;
    double saved_preview_time_ = 0;
    bool frame_exporting_ = false, saved_preview_ = false, saved_playing_ = false;
    double time_ = 0, offset_ = 0;
    bool ready_ = false, stale_ = false, preview_ = false, playing_ = false, pending_ = false,
         closing_ = false, locked_ = false;
    bool frame_source_current() const {
        if (!is_open() || !scene_ || !project_ || !context_ || !ready_ || stale_ || pending_ ||
            locked_ || scene_->asset_id() != controller_.scene || scene_->revision() != revision_ ||
            project_->project() != project_path_ || project_->generation() != project_generation_)
            return false;
        const auto actor = scene_->entity(controller_.entity.str());
        return actor && actor.has<AnimationSequence>() && actor.get<AnimationSequence>().enabled;
    }
    Json property(const std::string& id, const char* field, const Json& value,
                  const char* component = "forge.transform_animator") const {
        return {{"operation", "property.set"},
                {"arguments",
                 {{"entity", id}, {"component", component}, {"field", field}, {"value", value}}}};
    }
    void validate_shot(const std::string& id, const Json& camera, double start, double end,
                       bool enabled = true) const {
        const auto controller = scene_->entity(controller_.entity.str());
        if (!controller || !controller.has<AnimationSequence>())
            throw std::runtime_error("Sequence controller is no longer available");
        const auto duration = controller.get<AnimationSequence>().duration;
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || start >= end ||
            end > duration)
            throw std::runtime_error("Shot requires 0 <= start < end <= sequence duration");
        if (camera.is_null())
            throw std::runtime_error("Select a camera in this Scene");
        const auto reference = camera.get<EntityRef>();
        const auto target = scene_->entity(reference.entity.str());
        if (reference.scene != scene_->asset_id() || !target || !target.has<Camera>())
            throw std::runtime_error("Shot camera must be a Camera in this Scene");
        std::size_t count = 0;
        const auto snapshot = scene_->effective_document();
        for (const auto& row : snapshot.at("entities")) {
            const auto other_id = row.at("id").get<std::string>();
            if (other_id == id)
                continue;
            const auto actor = scene_->entity(other_id);
            if (!actor.has<CameraShot>())
                continue;
            const auto other = actor.get<CameraShot>();
            if (!other.enabled || !other.sequence.entity)
                continue;
            ++count;
            if (!enabled)
                continue;
            if (other.sequence != controller_) {
                const auto owner = scene_->entity(other.sequence.entity.str());
                if (other.sequence.scene == scene_->asset_id() && owner &&
                    owner.has<AnimationSequence>() && owner.get<AnimationSequence>().enabled)
                    throw std::runtime_error("Only one camera-shot sequence may direct this Scene");
            } else if (start < other.end_seconds && end > other.start_seconds)
                throw std::runtime_error(
                    "Shot overlaps another camera shot; use adjacent intervals");
        }
        if (enabled && count >= 256)
            throw std::runtime_error("Camera Shot limit (256) exceeded");
    }
    void apply_shot() {
        if (pending_ && scene_->revision() != pending_revision_)
            throw std::runtime_error(
                "Scene changed. Reset pending shot fields before applying them.");
        const auto actor = scene_->entity(shot_);
        if (!actor || !actor.has<CameraShot>() || actor.get<CameraShot>().sequence != controller_)
            throw std::runtime_error("Bound camera shot is no longer available");
        const auto value = actor.get<CameraShot>();
        validate_shot(shot_, shot_camera_, shot_start_, shot_end_, value.enabled);
        const auto start = property(shot_, "start_seconds", shot_start_, "forge.camera_shot");
        const auto end = property(shot_, "end_seconds", shot_end_, "forge.camera_shot");
        // Each draft command validates its interval. Order the two writes so
        // disjoint moves still have a valid intermediate draft; publication is atomic.
        auto commands =
            shot_end_ > value.end_seconds ? Json::array({end, start}) : Json::array({start, end});
        commands.push_back(property(shot_, "camera", shot_camera_, "forge.camera_shot"));
        apply_authoring(*scene_, commands, scene_->revision());
    }
    void draw_camera_shots(const Json& snapshot, const AnimationSequence& config,
                           const std::string& selected) {
        ImGui::SeparatorText("Camera shots");
        ImGui::TextWrapped("Shots switch the Game camera. Gaps use normal scene cameras. "
                           "Enable sequence preview, then open Game to view the result.");
        ImGui::BeginDisabled(pending_ || selected.empty() || !scene_->entity(selected) ||
                             !scene_->entity(selected).has<Camera>());
        if (ui::button("Add shot from selected camera",
                       "Create a shot in the next free interval "
                       "at or after the playhead; one Scene Undo step.")) {
            try {
                std::vector<CameraShot> spans;
                for (const auto& row : snapshot.at("entities")) {
                    const auto actor = scene_->entity(row.at("id").get<std::string>());
                    if (actor.has<CameraShot>()) {
                        const auto value = actor.get<CameraShot>();
                        if (value.enabled && value.sequence == controller_)
                            spans.push_back(value);
                    }
                }
                std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) {
                    return a.start_seconds < b.start_seconds;
                });
                double start = time_, end = std::min(start + 1, config.duration);
                for (const auto& span : spans) {
                    if (span.end_seconds <= start)
                        continue;
                    if (span.start_seconds > start) {
                        end = std::min(end, span.start_seconds);
                        break;
                    }
                    start = span.end_seconds;
                    end = std::min(start + 1, config.duration);
                }
                const Json camera = EntityRef{scene_->asset_id(), EntityId::parse(selected)};
                validate_shot({}, camera, start, end);
                const auto result = authoring_command(*scene_, "entity.create",
                                                      {{"recipe", "animation.camera_shot"},
                                                       {"component_values",
                                                        {{"sequence", controller_},
                                                         {"camera", camera},
                                                         {"start_seconds", start},
                                                         {"end_seconds", end}}}});
                shot_ = result.at("selected");
                track_.clear();
                cue_.clear();
                stop_preview();
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        FORGE_UI_PROBE("sequence:add_shot");
        ImGui::EndDisabled();
        for (const auto& row : snapshot.at("entities")) {
            if (!row.at("components").contains("forge.camera_shot"))
                continue;
            const auto id = row.at("id").get<std::string>();
            const auto actor = scene_->entity(id);
            if (!actor.has<CameraShot>())
                continue;
            const auto value = actor.get<CameraShot>();
            if (value.sequence != controller_)
                continue;
            ImGui::PushID(id.c_str());
            ImGui::BeginDisabled(pending_);
            if (ImGui::Selectable(row.value("name", id).c_str(), shot_ == id)) {
                shot_ = id;
                track_.clear();
                cue_.clear();
            }
            FORGE_UI_PROBE("sequence:shot:" + id);
            ImGui::EndDisabled();
            const auto camera =
                entity_ref_label(*scene_, value.camera.entity ? Json(value.camera) : Json());
            ImGui::TextDisabled("%s | %.3f - %.3f s%s", camera.c_str(), value.start_seconds,
                                value.end_seconds, value.enabled ? "" : " | Disabled");
            ui::help(("Camera: " + value.camera.entity.str() + "\nShot: " + id).c_str());
            const auto origin = ImGui::GetCursorScreenPos();
            const ImVec2 size{std::max(40.f, ImGui::GetContentRegionAvail().x),
                              24 * ui::interface_scale};
            ImGui::InvisibleButton("##shot span", size);
            FORGE_UI_PROBE("sequence:shot_span:" + id);
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                                IM_COL32(32, 42, 52, 255));
            const auto x = [&](double time) {
                return origin.x +
                       float(std::clamp(view_.fraction(time * animation_timebase), 0., 1.)) *
                           size.x;
            };
            draw->AddRectFilled({x(value.start_seconds), origin.y + 3},
                                {x(value.end_seconds), origin.y + size.y - 3},
                                value.enabled ? IM_COL32(120, 91, 168, 255)
                                              : IM_COL32(72, 76, 82, 255));
            draw->AddLine({x(time_), origin.y}, {x(time_), origin.y + size.y},
                          IM_COL32(255, 203, 78, 255), 2);
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
                time_ = view_.time((ImGui::GetIO().MousePos.x - origin.x) / size.x) /
                        animation_timebase;
                playing_ = false;
                if (ready_ && !stale_)
                    seek_preview();
            }
            ImGui::PopID();
        }
        const auto actor = shot_.empty() ? flecs::entity{} : scene_->entity(shot_);
        if (!actor || !actor.has<CameraShot>() || actor.get<CameraShot>().sequence != controller_)
            return;
        const auto value = actor.get<CameraShot>();
        if (!pending_) {
            shot_start_ = value.start_seconds;
            shot_end_ = value.end_seconds;
            shot_camera_ = value.camera.entity ? Json(value.camera) : Json();
        }
        const auto changed = [&] {
            if (!pending_)
                pending_revision_ = scene_->revision();
            pending_ = true;
        };
        if (entity_ref_picker(*scene_, shot_camera_, "Shot camera", false,
                              scene_->world().id<Camera>()))
            changed();
        FORGE_UI_PROBE("sequence:shot_camera");
        if (ImGui::InputDouble("Shot start", &shot_start_, 0, 0, "%.3f s"))
            changed();
        FORGE_UI_PROBE("sequence:shot_start");
        if (ImGui::InputDouble("Shot end", &shot_end_, 0, 0, "%.3f s"))
            changed();
        FORGE_UI_PROBE("sequence:shot_end");
        if (ui::button("Apply shot fields",
                       "Validate the interval/camera and commit one Scene Undo step."))
            apply_pending();
        FORGE_UI_PROBE("sequence:apply_shot");
        if (ui::button("Reset pending shot fields", "Reload the existing Scene draft values."))
            pending_ = false;
        FORGE_UI_PROBE("sequence:reset_shot");
        ImGui::BeginDisabled(pending_);
        if (ui::button("Select shot", "Show this shot's typed fields in Inspector.") &&
            ui::editor_context)
            ui::editor_context->selection.select_entity(shot_);
        if (ui::button("Remove shot from sequence",
                       "Retain this entity and camera; clear only its sequence binding.")) {
            try {
                apply_authoring(
                    *scene_,
                    Json::array({property(shot_, "sequence", nullptr, "forge.camera_shot")}),
                    scene_->revision());
                shot_.clear();
                cue_.clear();
                stop_preview();
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        FORGE_UI_PROBE("sequence:remove_shot");
        ImGui::EndDisabled();
    }
    void validate_cue(const Json& source, double time) const {
        const auto controller = scene_->entity(controller_.entity.str());
        if (!controller || !controller.has<AnimationSequence>() || !std::isfinite(time) ||
            time < 0 || time > controller.get<AnimationSequence>().duration)
            throw std::runtime_error("Cue time must be inside the current sequence duration");
        if (source.is_null())
            throw std::runtime_error("Choose an Audio Source in this Scene");
        const auto reference = source.get<EntityRef>();
        const auto actor = scene_->entity(reference.entity.str());
        if (reference.scene != scene_->asset_id() || !actor || !actor.has<AudioSource>() ||
            !actor.get<AudioSource>().clip.id)
            throw std::runtime_error(
                "Cue source must be an Audio Source with a clip in this Scene");
    }
    void apply_cue() {
        if (pending_ && scene_->revision() != pending_revision_)
            throw std::runtime_error(
                "Scene changed. Reset pending cue fields before applying them.");
        const auto actor = scene_->entity(cue_);
        if (!actor || !actor.has<AudioCue>() || actor.get<AudioCue>().sequence != controller_)
            throw std::runtime_error("Audio Cue is no longer bound to this sequence");
        validate_cue(cue_source_, cue_time_);
        authoring_command(*scene_, "property.set_many",
                          {{"entity", cue_},
                           {"component", "forge.audio_cue"},
                           {"values", {{"source", cue_source_}, {"time_seconds", cue_time_}}}});
    }
    void draw_audio_cues(const Json& snapshot, const AnimationSequence& config,
                         const std::string& selected) {
        ImGui::SeparatorText("Audio cues");
        ImGui::TextWrapped(
            "One-shot events during forward Play. Audio Source owns the clip and gain. "
            "Preview here is silent; sources keep their normal audio controls.");
        const auto source_actor = selected.empty() ? flecs::entity{} : scene_->entity(selected);
        ImGui::BeginDisabled(pending_ || !source_actor || !source_actor.has<AudioSource>() ||
                             !source_actor.get<AudioSource>().clip.id);
        if (ui::button("Add cue from selected Audio Source",
                       "Create a cue at the playhead in one Scene Undo step.")) {
            try {
                const Json source = EntityRef{scene_->asset_id(), EntityId::parse(selected)};
                const auto time = std::clamp(time_, 0., config.duration);
                validate_cue(source, time);
                if (scene_->world().query<const AudioCue>().count() >= 256)
                    throw std::runtime_error("Audio Cue component limit (256) exceeded");
                const auto result = authoring_command(
                    *scene_, "entity.create",
                    {{"recipe", "animation.audio_cue"},
                     {"component_values",
                      {{"sequence", controller_}, {"source", source}, {"time_seconds", time}}}});
                cue_ = result.at("selected");
                shot_.clear();
                track_.clear();
                stop_preview();
                error_.clear();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }
        FORGE_UI_PROBE("sequence:add_cue");
        ImGui::EndDisabled();
        for (const auto& row : snapshot.at("entities")) {
            if (!row.at("components").contains("forge.audio_cue"))
                continue;
            const auto id = row.at("id").get<std::string>();
            const auto actor = scene_->entity(id);
            if (!actor || !actor.has<AudioCue>() || actor.get<AudioCue>().sequence != controller_)
                continue;
            const auto value = actor.get<AudioCue>();
            ImGui::PushID(id.c_str());
            ImGui::BeginDisabled(pending_);
            if (ImGui::Selectable(row.value("name", id).c_str(), cue_ == id)) {
                cue_ = id;
                shot_.clear();
                track_.clear();
            }
            FORGE_UI_PROBE("sequence:cue:" + id);
            ImGui::EndDisabled();
            const auto source =
                entity_ref_label(*scene_, value.source.entity ? Json(value.source) : Json());
            ImGui::TextDisabled("%s | %.3f s%s", source.c_str(), value.time_seconds,
                                value.enabled ? "" : " | Disabled");
            const auto origin = ImGui::GetCursorScreenPos();
            const ImVec2 size{std::max(40.f, ImGui::GetContentRegionAvail().x),
                              24 * ui::interface_scale};
            ImGui::InvisibleButton("##cue point", size);
            FORGE_UI_PROBE("sequence:cue_point:" + id);
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y},
                                IM_COL32(32, 42, 52, 255));
            const auto x = [&](double seconds) {
                return origin.x +
                       float(std::clamp(view_.fraction(seconds * animation_timebase), 0., 1.)) *
                           size.x;
            };
            const auto fraction = view_.fraction(value.time_seconds * animation_timebase);
            if (fraction >= 0 && fraction <= 1) {
                const float point = x(value.time_seconds), mid = origin.y + size.y / 2;
                const float r = 5 * ui::interface_scale;
                const auto color =
                    value.enabled ? IM_COL32(74, 180, 155, 255) : IM_COL32(72, 76, 82, 255);
                draw->AddQuadFilled({point, mid - r}, {point + r, mid}, {point, mid + r},
                                    {point - r, mid}, color);
            }
            draw->AddLine({x(time_), origin.y}, {x(time_), origin.y + size.y},
                          IM_COL32(255, 203, 78, 255), 2);
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
                time_ = view_.time((ImGui::GetIO().MousePos.x - origin.x) / size.x) /
                        animation_timebase;
                playing_ = false;
                if (ready_ && !stale_)
                    seek_preview();
            }
            ImGui::PopID();
        }
        const auto actor = cue_.empty() ? flecs::entity{} : scene_->entity(cue_);
        if (!actor || !actor.has<AudioCue>() || actor.get<AudioCue>().sequence != controller_)
            return;
        const auto value = actor.get<AudioCue>();
        if (!pending_) {
            cue_source_ = value.source.entity ? Json(value.source) : Json();
            cue_time_ = value.time_seconds;
        }
        const auto changed = [&] {
            if (!pending_)
                pending_revision_ = scene_->revision();
            pending_ = true;
        };
        if (entity_ref_picker(*scene_, cue_source_, "Cue source", false,
                              scene_->world().id<AudioSource>()))
            changed();
        FORGE_UI_PROBE("sequence:cue_source");
        if (ImGui::InputDouble("Cue time", &cue_time_, 0, 0, "%.3f s"))
            changed();
        FORGE_UI_PROBE("sequence:cue_time");
        if (ui::button("Apply cue fields", "Commit source/time together in one Scene Undo step."))
            apply_pending();
        FORGE_UI_PROBE("sequence:apply_cue");
        if (ui::button("Reset pending cue fields", "Reload the current Scene draft values."))
            pending_ = false;
        FORGE_UI_PROBE("sequence:reset_cue");
        ImGui::BeginDisabled(pending_);
        if (ui::button("Select cue", "Show this cue in Inspector.") && ui::editor_context)
            ui::editor_context->selection.select_entity(cue_);
        FORGE_UI_PROBE("sequence:select_cue");
        if (ui::button("Remove cue from sequence",
                       "Retain the entity, source and time; clear its controller.")) {
            try {
                apply_authoring(
                    *scene_, Json::array({property(cue_, "sequence", nullptr, "forge.audio_cue")}),
                    scene_->revision());
                cue_.clear();
                stop_preview();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        }
        FORGE_UI_PROBE("sequence:remove_cue");
        ImGui::EndDisabled();
    }
    void seek_preview() {
        try {
            if (ready_ && context_)
                AnimationPlaybackAccess::control(
                    *context_->animation, controller_,
                    {{"operation", "seek"}, {"position_seconds", time_}});
        } catch (const std::exception& e) {
            error_ = e.what();
            stop_preview();
        }
    }
    bool apply_pending() {
        try {
            if (!cue_.empty()) {
                apply_cue();
                pending_ = false;
                stop_preview();
                error_.clear();
                return true;
            }
            if (!shot_.empty()) {
                apply_shot();
                pending_ = false;
                stop_preview();
                error_.clear();
                return true;
            }
            const auto controller = scene_->entity(controller_.entity.str());
            const auto target = track_.empty() ? flecs::entity{} : scene_->entity(track_);
            if (!controller || !controller.has<AnimationSequence>() || !target ||
                !target.has<TransformAnimator>() ||
                target.get<TransformAnimator>().sequence != controller_)
                throw std::runtime_error(
                    "Sequence controller or bound track is no longer available");
            const auto duration = controller.get<AnimationSequence>().duration;
            if (track_.empty() || !std::isfinite(offset_) || offset_ < 0 || offset_ > duration)
                throw std::runtime_error("Track start must be inside the sequence");
            if (pending_ && scene_->revision() != pending_revision_)
                throw std::runtime_error(
                    "Scene changed. Reset the pending start before applying it.");
            apply_authoring(*scene_,
                            Json::array({property(track_, "sequence_start_seconds", offset_)}),
                            scene_->revision());
            pending_ = false;
            stop_preview();
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    void refresh() {
        stop_preview();
        ready_ = false;
        context_.reset();
        error_.clear();
        targets_.clear();
        try {
            if (frame_export_serial_ == std::numeric_limits<std::uint64_t>::max())
                throw std::runtime_error("Sequence preview generation exhausted");
            ++frame_export_serial_;
            const auto config = scene_->entity(controller_.entity.str()).get<AnimationSequence>();
            time_ = std::clamp(time_, 0., config.duration);
            catalog_ = std::make_shared<const AssetCatalog>(
                AssetCatalog::open_project(project_->project()));
            revision_ = scene_->revision();
            stale_ = false;
            context_ =
                std::make_unique<Context>(project_->project(), scene_->snapshot(), controller_);
            targets_ = context_->targets;
        } catch (const std::exception& e) {
            error_ = e.what();
        }
    }
};
} // namespace forge
