#pragma once
#include "../texture_authoring.hpp"
#include "../transform_animation_authoring.hpp"
#include "../transform_animation_preview.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include <cmath>
#include <forge/authoring.hpp>
#include <optional>
namespace forge {
class AnimationTimelineEditor {
  public:
    bool close_cancelled = false;
    bool is_open() const { return bool(document_); }
    bool pending() const { return job_ != 0; }
    bool dirty() const { return document_ && (document_->dirty() || needs_publish_ || pending()); }
    bool can_undo() const { return document_ && !pending() && document_->can_undo(); }
    bool can_redo() const { return document_ && !pending() && document_->can_redo(); }
    const TransformAnimationDocument* document() const { return document_.get(); }
    std::shared_ptr<const AssetCatalog> take_catalog() { return std::exchange(published_, {}); }
    void request_save() { save_ = true; }
    void request_close() {
        stop_preview();
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
        if (play)
            stop_preview();
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
            channels_controls();
            double seconds = time_ / animation_timebase;
            if (ImGui::SliderScalar("Playhead (seconds)", ImGuiDataType_Double, &seconds, &zero_,
                                    &duration, "%.3f")) {
                time_ = seconds * animation_timebase;
                key_ = {};
                fields(clip_->sample(time_));
                preview_ = bool(target_.entity);
                playing_ = false;
            }
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
    AssetJobId job_ = 0;
    bool needs_publish_ = false, save_ = false, close_ = false, preview_ = false, playing_ = false,
         preview_loop_ = true;
    double time_ = 0, zero_ = 0, drag_time_ = 0;
    float preview_speed_ = 1;
    double position_[3]{};
    float rotation_[3]{}, scale_[3]{1, 1, 1};
    bool held_ = false, curve_dirty_ = false;
    TransformAnimationCurves curves_;
    int curve_channel_ = 0, curve_axis_ = 0;
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
        stop_preview();
        document_.reset();
        clip_.reset();
        service_.reset();
        target_ = {};
        key_ = drag_key_ = {};
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
        select_key(clip_->keys.front().id);
        error_.clear();
    }
    void changed() {
        clip_ = document_->source().clip();
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
        drag_key_ = {};
    }
    template <class F> void mutate(std::string label, F edit) {
        try {
            const auto revision = document_->revision();
            document_->edit(revision, std::move(label), [&](Json& draft) {
                TransformAnimationSource source{draft};
                edit(source);
                draft = std::move(source.document);
            });
            if (document_->revision() != revision)
                changed();
            error_.clear();
        } catch (const std::exception& e) {
            report("edit", e.what());
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
    void ruler() {
        const auto start = ImGui::GetCursorScreenPos();
        const float width = std::max(140.f, ImGui::GetContentRegionAvail().x), height = 100;
        ImGui::InvisibleButton("Animation ruler", {width, height});
        FORGE_UI_PROBE("animation:ruler");
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, {start.x + width, start.y + height}, IM_COL32(19, 27, 36, 255));
        const auto x = [&](double time) { return start.x + float(time / clip_->duration) * width; };
        for (int i = 0; i <= 10; ++i) {
            const float at = start.x + width * i / 10;
            draw->AddLine({at, start.y}, {at, start.y + height}, IM_COL32(50, 63, 75, 255));
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f",
                          double(clip_->duration) / animation_timebase * i / 10);
            draw->AddText({at + 2, start.y + 2}, IM_COL32(180, 190, 200, 255), text);
        }
        for (const auto& key : clip_->keys) {
            const float at = x(drag_key_ == key.id ? drag_time_ : double(key.time));
            for (int row = 0; row < 3; ++row)
                if (clip_->channels & (1u << row)) {
                    const float y = start.y + 36 + row * 22;
                    const auto color =
                        key.id == key_ ? IM_COL32(255, 203, 78, 255) : IM_COL32(90, 177, 229, 255);
                    draw->AddQuadFilled({at, y - 5}, {at + 5, y}, {at, y + 5}, {at - 5, y}, color);
                }
        }
        draw->AddLine({x(time_), start.y}, {x(time_), start.y + height},
                      IM_COL32(110, 235, 190, 255), 2);
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
            const float mouse = ImGui::GetIO().MousePos.x;
            AssetId selected;
            for (const auto& key : clip_->keys)
                if (std::abs(x(double(key.time)) - mouse) < 7) {
                    selected = key.id;
                    break;
                }
            if (selected) {
                select_key(selected);
                drag_key_ = selected;
                drag_time_ = time_;
            } else {
                key_ = {};
                time_ = std::clamp(double(mouse - start.x) / width, 0., 1.) * clip_->duration;
                fields(clip_->sample(time_));
            }
            preview_ = bool(target_.entity);
            playing_ = false;
        }
        if (drag_key_ && ImGui::IsMouseDragging(0)) {
            drag_time_ =
                std::round(std::clamp(double(ImGui::GetIO().MousePos.x - start.x) / width, 0., 1.) *
                           clip_->duration / 1000) *
                1000;
            time_ = drag_time_;
        }
        if (drag_key_ && ImGui::IsMouseReleased(0)) {
            const auto id = std::exchange(drag_key_, {});
            const auto time = std::int64_t(drag_time_);
            mutate("Move animation key", [&](auto& source) { source.move_key(id, time); });
        }
        if (drag_key_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            select_key(drag_key_);
            drag_key_ = {};
        }
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
            mutate("Set animation key", [&](auto& source) {
                const LocalTransform pose{
                    {position_[0], position_[1], position_[2]},
                    rotation_from_euler({rotation_[0], rotation_[1], rotation_[2]}),
                    checked_local_scale({scale_[0], scale_[1], scale_[2]})};
                key_ = source.set_key(
                    std::llround(time_), pose, held_, {},
                    curve_dirty_ ? std::optional<TransformAnimationCurves>(curves_) : std::nullopt);
            });
        }
        ImGui::BeginDisabled(!key_ || clip_->keys.size() == 1);
        if (ui::button(
                "Delete selected key",
                "Remove this key as one Undo step; an animation must retain at least one key.")) {
            const auto selected = key_;
            mutate("Delete animation key", [&](auto& source) { source.erase_key(selected); });
        }
        ImGui::EndDisabled();
        if (ImGui::BeginListBox("Keys")) {
            for (const auto& key : clip_->keys) {
                char label[64];
                std::snprintf(label, sizeof(label), "%.3f s — %s",
                              double(key.time) / animation_timebase,
                              key.held           ? "Held"
                              : key.curves.cubic ? "Cubic vectors"
                                                 : "Linear");
                ImGui::PushID(key.id.str().c_str());
                if (ImGui::Selectable(label, key.id == key_))
                    select_key(key.id);
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
        curve_editor();
    }

    void curve_editor() {
        const auto expanded = ImGui::CollapsingHeader("Vector curves");
        FORGE_UI_PROBE("animation:curve_section");
        if (!expanded)
            return;
        const auto selected = std::find_if(clip_->keys.begin(), clip_->keys.end(),
                                           [&](const auto& key) { return key.id == key_; });
        if (selected == clip_->keys.end()) {
            ImGui::TextUnformatted("Select a key to edit its incoming and outgoing vector slopes.");
            return;
        }
        const auto key = *selected;
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
                "Pending slopes: apply the curve settings to update the authored curve.");
        ImGui::Combo("Curve value", &curve_channel_, "Position\0Scale\0");
        FORGE_UI_PROBE("animation:curve_channel");
        ImGui::SameLine();
        ImGui::Combo("Axis", &curve_axis_, "X\0Y\0Z\0");
        FORGE_UI_PROBE("animation:curve_axis");
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
        ImGui::InvisibleButton("Curve view", size);
        FORGE_UI_PROBE("animation:curve_canvas");
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
        const double padding = std::max(.1, (maximum - minimum) * .1);
        minimum -= padding;
        maximum += padding;
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
            const double control_value = component(key.pose) + (incoming ? -1 : 1) * slope *
                                                                   interval /
                                                                   (3 * animation_timebase);
            if (!std::isfinite(control_value))
                return;
            // Intersect the true slope with the view boundary. Clamping only
            // Y at the original control time would draw an incorrect slope.
            const auto start_value = component(key.pose);
            const auto visible_value = std::clamp(control_value, minimum, maximum);
            const auto fraction = control_value == start_value ? 1
                                                               : (visible_value - start_value) /
                                                                     (control_value - start_value);
            const auto handle =
                point(double(key.time) + (control_time - key.time) * fraction, visible_value);
            draw->AddLine(point(double(key.time), component(key.pose)), handle,
                          IM_COL32(244, 183, 82, 255), 1);
            if (control_value >= minimum && control_value <= maximum)
                draw->AddCircleFilled(handle, 3, IM_COL32(244, 183, 82, 255));
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
        ImGui::TextDisabled("Selected key and neighboring segments; blue curve is authored, gold "
                            "guides use the slope fields.");
        ImGui::Text("%.3f–%.3f s | %.3f–%.3f", begin / animation_timebase, end / animation_timebase,
                    minimum, maximum);
        if (ui::button("Apply curve settings",
                       "Apply only this key's vector curve mode/slopes as one source Undo step.")) {
            mutate("Set vector curves", [&](auto& source) {
                source.set_key(key.time, key.pose, key.held, key.id, curves_);
            });
        }
    }
};
} // namespace forge
