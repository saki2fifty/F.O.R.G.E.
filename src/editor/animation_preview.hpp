#pragma once
#include "../animation_preview.hpp"
#include "../animation_states_resource.hpp"
#include "document.hpp"
#include "editor_state.hpp"
#include <forge/animation_blend_space.hpp>
#include <forge/authoring.hpp>
#include <imgui.h>
namespace forge {
class AnimationPreviewEditor {
  public:
    bool is_open() const { return open_; }
    bool preview_active() const { return preview_.active(); }
    void stop_preview() {
        preview_.stop();
        playing_ = false;
    }
    void close() {
        stop_preview();
        open_ = false;
    }
    void open(SceneDocument& project, Scene& scene, AssetId clip = {}) {
        close();
        target_ = {};
        name_ = "Choose an Animator";
        open_ = true;
        project_ = project.project();
        if (ui::editor_context && !ui::editor_context->selection.entity().empty()) {
            const auto selected = scene.entity(ui::editor_context->selection.entity());
            if (selected.has<Animator>() && (!clip || selected.get<Animator>().clip.id == clip ||
                                             selected.get<Animator>().blend_clip.id == clip))
                bind(scene, ui::editor_context->selection.entity());
        }
        const auto source = scene.document();
        if (!target_.entity)
            for (const auto& item : source.at("entities")) {
                auto actor = scene.entity(item.at("id"));
                if (actor.has<Animator>() && (!clip || actor.get<Animator>().clip.id == clip ||
                                              actor.get<Animator>().blend_clip.id == clip)) {
                    bind(scene, item.at("id"));
                    break;
                }
            }
    }
    void content(SceneDocument& project, Scene& scene, bool locked) {
        ImGui::BeginDisabled(locked);
        if (ui::button("Preview skeletal animation...",
                       "Scrub and blend an object's Animator in a temporary Scene view."))
            open(project, scene);
        ImGui::EndDisabled();
    }
    void poll(SceneDocument& project, Scene& scene, bool play, float dt) {
        if (!open_)
            return;
        if (project.project() != project_) {
            close();
            target_ = {};
            return;
        }
        if (play || scene.revision() != revision_)
            stop_preview();
        if (target_.entity &&
            (target_.scene != scene.asset_id() || !scene.entity(target_.entity.str()))) {
            stop_preview();
            target_ = {};
        }
        if (scene.revision() != revision_) {
            if (target_.entity)
                bind(scene, target_.entity.str());
            revision_ = scene.revision();
        }
        try {
            if (preview_active() && preview_.prepare()) {
                preview_.configure(weight_, seconds_, loop_, speed_);
                if (states_asset_)
                    preview_.select_state(state_);
                if (space_asset_)
                    preview_.select_blend_position(position_);
                if (playing_ && dt > 0)
                    preview_.tick(std::min(dt, .25f));
                auto pose = preview_.pose();
                time_ = pose.at("time");
                duration_ = pose.at("duration");
                evaluated_weight_ = pose.at("blend_weight");
                if (!pose.at("playing").get<bool>())
                    playing_ = false;
            }
        } catch (const std::exception& e) {
            fail(e);
        }
    }
    Json snapshot(Json authored) {
        try {
            auto result = preview_.snapshot();
            return result.is_null() ? authored : result;
        } catch (const std::exception& e) {
            fail(e);
            return authored;
        }
    }
    void draw(SceneDocument& project, Scene& scene, bool locked) {
        if (!open_)
            return;
        const bool expanded = ImGui::Begin("Animation Preview###Animation Preview", &open_);
        if (!open_)
            stop_preview();
        if (ui::editor_context && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            ui::editor_context->task.focus_document("animation_preview", "Animation Preview");
        if (expanded) {
            ImGui::TextWrapped(
                "Preview an Animator here. Set its primary and second clips in Inspector.");
            ImGui::BeginDisabled(locked);
            if (ImGui::BeginCombo("Preview object", name_.c_str())) {
                const auto source = scene.document();
                for (const auto& item : source.at("entities")) {
                    const auto id = item.at("id").get<std::string>();
                    if (!scene.entity(id).has<Animator>())
                        continue;
                    if (ImGui::Selectable((item.at("name").get<std::string>() + "##" + id).c_str(),
                                          target_.entity.str() == id))
                        bind(scene, id);
                }
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(!target_.entity);
            if (ui::button(
                    playing_ ? "Pause preview" : "Play preview",
                    "Play the copied animation world; authoring and physics stay unchanged.")) {
                try {
                    ensure(project, scene);
                    if (preview_.prepare() && time_ >= duration_)
                        preview_.seek(0);
                    playing_ = !playing_;
                } catch (const std::exception& e) {
                    fail(e);
                }
            }
            ImGui::SameLine();
            if (ui::button("Stop preview", "Restore the authored Scene view."))
                stop_preview();
            const bool scrubbed = preview_active() && duration_ > 0 &&
                                  ImGui::SliderScalar("Playhead (seconds)", ImGuiDataType_Double,
                                                      &time_, &zero_, &duration_, "%.3f");
            if (preview_active() && duration_ > 0)
                FORGE_UI_PROBE("animation_preview:playhead");
            if (scrubbed) {
                try {
                    preview_.seek(time_);
                    playing_ = false;
                } catch (const std::exception& e) {
                    fail(e);
                }
            }
            if (states_asset_) {
                const bool states_open = ImGui::BeginCombo("Preview state", state_name().c_str());
                FORGE_UI_PROBE("animation_preview:state_combo");
                if (states_open) {
                    for (const auto& item : named_states_.states) {
                        if (ImGui::Selectable(item.name.c_str(), state_ == item.id))
                            state_ = item.id;
                        FORGE_UI_PROBE("animation_preview:state:" + std::to_string(item.id));
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextUnformatted(
                    "Named states own clip, loop, speed and fade. Changes here are temporary.");
            }
            bool changed = false;
            if (space_asset_) {
                ImGui::TextUnformatted("Blend Space: neighboring clips share one cycle phase. "
                                       "Changes here are temporary.");
                changed |= ImGui::SliderFloat("Blend position", &position_, minimum_position_,
                                              maximum_position_, "%.3f");
                FORGE_UI_PROBE("animation_preview:position");
                if (changed && preview_active()) {
                    try {
                        preview_.select_blend_position(position_);
                    } catch (const std::exception& e) {
                        fail(e);
                    }
                }
            }
            const bool can_blend =
                target_.entity && scene.entity(target_.entity.str()).has<Animator>() &&
                bool(scene.entity(target_.entity.str()).get<Animator>().blend_clip.id);
            if (can_blend)
                ImGui::TextUnformatted(
                    scene.entity(target_.entity.str()).get<Animator>().blend_mode == 1
                        ? "Additive overlay: adds offsets to the base clip."
                        : "Blend: mixes the two clip poses.");
            ImGui::BeginDisabled(!can_blend);
            changed |= ImGui::SliderFloat("Blend target", &weight_, 0, 1, "%.3f");
            FORGE_UI_PROBE("animation_preview:weight");
            changed |= ImGui::SliderFloat("Transition (seconds)", &seconds_, 0, 10, "%.3f");
            FORGE_UI_PROBE("animation_preview:seconds");
            ImGui::EndDisabled();
            ImGui::BeginDisabled(bool(states_asset_));
            changed |= ImGui::Checkbox("Loop preview", &loop_);
            ImGui::EndDisabled();
            changed |= ImGui::SliderFloat("Preview speed", &speed_, 0, 4, "%.2fx");
            if (changed && preview_active()) {
                try {
                    preview_.configure(weight_, seconds_, loop_, speed_);
                } catch (const std::exception& e) {
                    fail(e);
                }
            }
            if (ui::button("Apply preview settings to Animator",
                           "Save these settings to the authored Scene as one Undo step.")) {
                try {
                    auto commands = Json::array();
                    Json settings{{"blend_weight", weight_},
                                  {"transition_seconds", seconds_},
                                  {"loop", loop_},
                                  {"playback_speed", speed_}};
                    if (states_asset_)
                        settings = {{"state", state_}, {"playback_speed", speed_}};
                    if (space_asset_)
                        settings = {{"blend_position", position_},
                                    {"loop", loop_},
                                    {"playback_speed", speed_}};
                    for (const auto& [field, value] : settings.items())
                        commands.push_back({{"operation", "property.set"},
                                            {"arguments",
                                             {{"entity", target_.entity.str()},
                                              {"component", "forge.animator"},
                                              {"field", field},
                                              {"value", value}}}});
                    apply_authoring(scene, commands, revision_);
                    bind(scene, target_.entity.str());
                } catch (const std::exception& e) {
                    fail(e);
                }
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (preview_active())
                ImGui::TextColored({.4f, .85f, 1, 1},
                                   "PREVIEW — temporary Scene view | evaluated mix %.3f",
                                   evaluated_weight_);
            if (!error_.empty())
                ui::field_error(error_);
        }
        ImGui::End();
    }
    void bind(Scene& scene, const std::string& id) {
        stop_preview();
        const auto actor = scene.entity(id);
        if (!actor.has<Animator>()) {
            target_ = {};
            return;
        }
        target_ = scene.reference(id);
        const auto config = actor.get<Animator>();
        states_asset_ = config.states.id;
        space_asset_ = config.blend_space.id;
        position_ = config.blend_position;
        state_ = config.state;
        named_states_ = {};
        if (states_asset_) {
            try {
                const auto catalog = AssetCatalog::open_project(project_);
                const auto selected = catalog.resolve(config.states);
                if (selected.state != AssetState::Available)
                    throw std::runtime_error(selected.diagnostic);
                const auto& record = *selected.record;
                DerivedDataCache cache(project_, {animation_states_byte_limit,
                                                  animation_states_byte_limit + 65536, 1});
                cache.load_selected(
                    record.metadata.at("forge.import").at("key").get<std::string>(),
                    [&](const auto& artifact) {
                        if (artifact.files.size() != 1 ||
                            artifact.files.front().name != "states.json")
                            throw std::runtime_error("Invalid Animation States preview artifact");
                        named_states_ =
                            decode_animation_states(artifact.files.front().bytes, states_asset_);
                    });
                state_ = named_states_.state(state_).id;
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        if (space_asset_) {
            try {
                const auto catalog = AssetCatalog::open_project(project_);
                const auto resolved = catalog.resolve(config.blend_space);
                if (resolved.state != AssetState::Available)
                    throw std::runtime_error(resolved.diagnostic);
                DerivedDataCache cache(project_,
                                       {animation_blend_space_cooked_byte_limit,
                                        animation_blend_space_cooked_byte_limit + 65536, 1});
                cache.load_selected(
                    resolved.record->metadata.at("forge.import").at("key").get<std::string>(),
                    [&](const auto& artifact) {
                        if (artifact.files.size() != 1 ||
                            artifact.files.front().name != "blendspace.json")
                            throw std::runtime_error("Invalid Blend Space preview artifact");
                        const auto space = decode_animation_blend_space(
                            artifact.files.front().bytes, space_asset_);
                        minimum_position_ = float(space.samples.front().position);
                        maximum_position_ = float(space.samples.back().position);
                    });
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        weight_ = config.blend_weight;
        seconds_ = config.transition_seconds;
        speed_ = config.playback_speed;
        loop_ = config.loop;
        revision_ = scene.revision();
        time_ = duration_ = 0;
        name_ = actor.name().c_str();
        if (!states_asset_ && !space_asset_)
            error_.clear();
    }

  private:
    SkeletalAnimationPreview preview_;
    AnimationStates named_states_;
    AssetId states_asset_;
    AssetId space_asset_;
    float position_ = 0, minimum_position_ = 0, maximum_position_ = 1;
    std::uint32_t state_ = 0;
    std::string state_name() const {
        try {
            return named_states_.state(state_).name;
        } catch (...) {
            return "Unavailable";
        }
    }
    std::filesystem::path project_;
    EntityRef target_{};
    std::uint64_t revision_ = 0;
    bool open_ = false, playing_ = false, loop_ = true;
    float weight_ = 0, seconds_ = .25f, speed_ = 1, evaluated_weight_ = 0;
    double time_ = 0, duration_ = 0, zero_ = 0;
    std::string name_ = "Choose an Animator", error_;
    void ensure(SceneDocument& project, Scene& scene) {
        if (!preview_active())
            preview_.start(project.project(), scene.document(), target_);
        if (preview_.prepare())
            preview_.configure(weight_, seconds_, loop_, speed_);
    }
    void fail(const std::exception& e) {
        error_ = e.what();
        stop_preview();
        ui::report_error("animation_preview", error_);
    }
};
} // namespace forge
