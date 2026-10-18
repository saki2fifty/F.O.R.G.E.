#pragma once
#include "editor/animation_timeline.hpp"
#include "editor/view_image.hpp"
#include <array>
namespace forge::test {
// Test-only real editor workflow. OS folder selection is the sole injected action.
class SequenceFrameEditorWorkflow {
    enum class Step {
        Prepare,
        Open,
        Settings,
        Cancel,
        WaitCancel,
        Reopen,
        Count,
        Fps,
        Parent,
        Name,
        Start,
        Wait,
        Done
    };
    Step step_ = Step::Prepare;
    unsigned group_ = 0, click_ = 0, typing_ = 0, stable_ = 0, preparation_ready_ = 0;
    ImVec2 point_{-FLT_MAX, -FLT_MAX};
    float wheel_ = 0;
    std::string text_;
    Uint64 started_ = SDL_GetTicks(), phase_started_ = started_;
    bool opened_ = false, source_opened_ = false, picker_seeded_ = false;
    Json authored_, records_ = Json::array();
    std::uint64_t revision_ = 0;
    std::array<float, 4> layout_{};
    void advance(Step next) {
        step_ = next;
        stable_ = click_ = typing_ = 0;
        picker_seeded_ = false;
        point_ = {-FLT_MAX, -FLT_MAX};
        wheel_ = 0;
        phase_started_ = SDL_GetTicks();
    }
    static bool visible(const UiTarget& box) {
        return box.enabled && box.minimum.x >= box.clip_minimum.x &&
               box.maximum.x <= box.clip_maximum.x && box.minimum.y >= box.clip_minimum.y &&
               box.maximum.y <= box.clip_maximum.y;
    }
    bool click(const char* id) {
        if (click_ == 4) {
            click_ = 0;
            return true;
        }
        if (click_)
            return false;
        const auto found = ui_targets.find(id);
        if (found == ui_targets.end() || !visible(found->second)) {
            stable_ = 0;
            if (found != ui_targets.end() && found->second.enabled) {
                const auto& box = found->second;
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    point_ = {(box.clip_minimum.x + box.clip_maximum.x) / 2,
                              (box.clip_minimum.y + box.clip_maximum.y) / 2};
                    wheel_ = box.minimum.y < box.clip_minimum.y ? .25f : -.25f;
                }
            }
            return false;
        }
        const auto& box = found->second;
        const ImVec2 candidate{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
        stable_ = std::abs(candidate.x - point_.x) < .5f && std::abs(candidate.y - point_.y) < .5f
                      ? stable_ + 1
                      : 1;
        point_ = candidate;
        if (stable_ >= 3)
            click_ = 1;
        return false;
    }
    bool edit(const char* id, const std::string& value) {
        if (!typing_ && click(id)) {
            text_ = value;
            typing_ = 1;
        }
        if (typing_ != 5)
            return false;
        typing_ = 0;
        return true;
    }

  public:
    void input() {
        auto& io = ImGui::GetIO();
        io.ConfigInputTrickleEventQueue = false;
        io.AddMousePosEvent(point_.x, point_.y);
        if (wheel_) {
            io.AddMouseWheelEvent(0, wheel_);
            wheel_ = 0;
        }
        if (click_ == 1)
            click_ = 2; // One hover frame before press.
        else if (click_ == 2) {
            io.AddMouseButtonEvent(0, true);
            click_ = 3;
        } else if (click_ == 3) {
            io.AddMouseButtonEvent(0, false);
            click_ = 4;
        }
        if (typing_ == 1) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            typing_ = 2;
        } else if (typing_ == 2) {
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddInputCharactersUTF8(text_.c_str());
            typing_ = 3;
        } else if (typing_ == 3) {
            io.AddKeyEvent(ImGuiKey_Enter, true);
            typing_ = 4;
        } else if (typing_ == 4) {
            io.AddKeyEvent(ImGuiKey_Enter, false);
            typing_ = 5;
        }
    }
    void prepare(EditorFixture& fixture, SceneDocument& project, Scene& scene,
                 AnimationTimelineEditor& timeline, SDL_Window* window, bool sources_ready) {
        observe_ui = true;
        ui_targets.clear();
        if (!opened_) {
            SDL_SetWindowSize(window, 1920, 1080);
            project.open_scene("Scenes/70-sequence-audio-cues.scene.json");
            opened_ = true;
            return; // Let normal source import react to the newly opened Scene.
        }
        if (step_ != Step::Prepare)
            return;
        ui::style(1.f + .5f * group_);
        preparation_ready_ = sources_ready ? preparation_ready_ + 1 : 0;
        if (preparation_ready_ < 3)
            return;
        if (!fixture.view_image_sources_ready)
            fixture.snapshot_view_image_sources();
        if (!source_opened_) {
            const auto snapshot = scene.snapshot();
            for (const auto& row : snapshot.at("entities"))
                if (row.at("components").contains("forge.animation_sequence"))
                    timeline.open_sequence(project, scene,
                                           {scene.asset_id(), row.at("id").get<EntityId>()});
            source_opened_ = true;
        }
        if (timeline.frame_export_ready()) {
            authored_ = scene.document();
            revision_ = scene.revision();
            advance(Step::Open);
        }
    }
    template <class Capture>
    bool finish(EditorFixture& fixture, AnimationTimelineEditor& timeline, ui::ViewImageTask& task,
                Scene& scene, bool playing, bool game_ready, Capture&& capture) {
        const auto fail = [&](const std::string& message) {
            Json state{{"step", int(step_)},
                       {"scale", ui::interface_scale},
                       {"task", task.status()},
                       {"message", message},
                       {"source_ready", timeline.frame_export_ready()},
                       {"game_ready", game_ready}};
            atomic_write(fixture.output / "sequence-frames-failed.json", state.dump(2));
            capture("sequence-frames-failed");
            throw std::runtime_error(message);
        };
        if (SDL_GetTicks() - started_ > 240000 || SDL_GetTicks() - phase_started_ > 90000)
            fail("Sequence frame editor workflow timed out");
        if (playing || (source_opened_ && scene.document() != authored_ && step_ != Step::Prepare))
            fail("Sequence export entered Play or changed authored Scene values");
        const auto suffix = std::to_string(100 + group_ * 50);
        if (step_ == Step::Open || step_ == Step::Reopen) {
            if (auto* window = ImGui::FindWindowByName("###Animation Timeline")) {
                ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
                ImGui::SetWindowPos(window, {30, 55}, ImGuiCond_Always);
                ImGui::SetWindowSize(window, {1860, 990}, ImGuiCond_Always);
                ImGui::SetWindowFocus("###Animation Timeline");
            }
            const bool reopening = step_ == Step::Reopen;
            if (click("sequence:export-frames"))
                advance(reopening ? Step::Count : Step::Settings);
        } else if (step_ == Step::Settings) {
            auto* modal = ImGui::FindWindowByName("Export sequence frames");
            const auto start = ui_targets.find("frame-export:start");
            const auto cancel = ui_targets.find("frame-export:cancel");
            const auto* focus = ImGui::GetCurrentContext()->NavWindow;
            const bool ready = modal && focus && modal->RootWindow == focus->RootWindow &&
                               start != ui_targets.end() && visible(start->second) &&
                               cancel != ui_targets.end() && visible(cancel->second) &&
                               game_ready && timeline.preview_active();
            if (ready) {
                const std::array layout{modal->Pos.x, modal->Pos.y, modal->Size.x, modal->Size.y};
                stable_ = layout == layout_ ? stable_ + 1 : 1;
                layout_ = layout;
                if (stable_ >= 3) {
                    capture("sequence-frame-settings-" + suffix);
                    records_.push_back({{"scale", ui::interface_scale},
                                        {"controls_ready", true},
                                        {"modal_in_front", true},
                                        {"game_preview_ready", true},
                                        {"stable_frames", stable_}});
                    advance(Step::Cancel);
                }
            } else
                stable_ = 0;
        } else if (step_ == Step::Cancel) {
            if (click("frame-export:cancel"))
                advance(Step::WaitCancel);
        } else if (step_ == Step::WaitCancel) {
            if (!task.busy()) {
                if (timeline.preview_active() || scene.revision() != revision_)
                    fail("Configuration cancel failed to restore stopped preview/history");
                advance(Step::Reopen);
            }
        } else if (step_ == Step::Count) {
            if (edit("frame-export:count", "4"))
                advance(Step::Fps);
        } else if (step_ == Step::Fps) {
            if (edit("frame-export:fps", "1"))
                advance(Step::Parent);
        } else if (step_ == Step::Parent) {
            if (!picker_seeded_) {
                FileDialog::fixture_selection = FileDialog::Result{
                    FileDialog::Kind::SequenceFramesParent, path_utf8(fixture.output), {}, {}};
                picker_seeded_ = true;
            }
            if (click("frame-export:browse"))
                advance(Step::Name);
        } else if (step_ == Step::Name) {
            if (edit("frame-export:name", "Frames" + suffix))
                advance(Step::Start);
        } else if (step_ == Step::Start) {
            if (click("frame-export:start"))
                advance(Step::Wait);
        } else if (step_ == Step::Wait && !task.busy()) {
            if (!task.status().starts_with("Sequence frames saved:") || timeline.preview_active() ||
                scene.revision() != revision_)
                fail("Native sequence frame export failed or did not restore preview/history");
            if (++group_ == 3) {
                atomic_write(fixture.output / "sequence-frame-workflow.json",
                             Json{{"source_commit", source_commit},
                                  {"build_id", build_id},
                                  {"physical_picker", false},
                                  {"picker_injected", true},
                                  {"captures", records_},
                                  {"authored_unchanged", true}}
                                 .dump(2));
                advance(Step::Done);
            } else
                advance(Step::Prepare);
        }
        return step_ == Step::Done;
    }
};
} // namespace forge::test
