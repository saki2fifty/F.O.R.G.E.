#pragma once
#include "audio_source_access.hpp"
#include "imgui_software_capture.hpp"
#include <forge/audio.hpp>

void capture_sound_cue_editor(const std::filesystem::path& gallery,
                              const std::filesystem::path& output, bool timed = false,
                              bool repeating = false, bool completion = false) {
    std::filesystem::create_directories(output);
    const auto root = output / "project";
    require(!std::filesystem::exists(root), "Cue capture project already exists");
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        auto capture = std::make_unique<test::ImGuiSoftwareCapture>(700, repeating ? 1280 : 1100);
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        project.open_scene(root / (completion  ? "Scenes/195-completion-sound-cues.scene.json"
                                   : repeating ? "Scenes/194-repeating-sound-cues.scene.json"
                                   : timed     ? "Scenes/193-timed-sound-cues.scene.json"
                                               : "Scenes/192-event-sound-cues.scene.json"));
        std::string cue;
        const auto effective = scene.effective_document();
        for (const auto& row : effective.at("entities"))
            if (row.at("components").contains("forge.sound_cue"))
                cue = row.at("id");
        require(!cue.empty(), "Capture has no Sound Cue");
        const auto before = scene.snapshot();
        ui::EditorUiContext owner;
        owner.scene = &scene;
        ui::ContextScope context(owner);
        SoundCueEditor editor;
        AudioSourceEditor live;
        AudioConfig config;
        config.project = root;
        config.output = AudioOutput::Offline;
        config.required = true;
        config.routing =
            audio_routing_settings(ProjectSettings(root).document().at("game").at("audio"));
        EngineContext runtime_engine(WorldRole::Runtime, false, {audio_module(std::move(config))});
        Scene runtime_scene(runtime_engine.world());
        runtime_scene.restore_snapshot(before);
        auto audio = std::static_pointer_cast<AudioRuntime>(runtime_engine.services().audio());
        audio->synchronize(0);
        require(audio->wait_for_sources(std::chrono::seconds(5)),
                "Cue capture audio did not prepare");
        const auto reference = runtime_scene.reference(cue);
        if (repeating) {
            audio->paused(false);
            if (completion) {
                unsigned quanta = 0;
                while (AudioSourceAccess::status(*audio, reference).at("accepted_triggers") < 4) {
                    require(++quanta <= 32768, "Completion capture did not reach four events");
                    std::array<float, 512> pcm{};
                    audio->read_offline(pcm);
                }
            } else {
                std::vector<float> pcm((3 * 24576 + 1) * 2);
                audio->read_offline(pcm);
            }
        } else {
            for (unsigned i = 0; i < 4; ++i)
                AudioSourceAccess::control(*audio, reference, AudioSourceOperation::OneShot);
        }
        const auto status = AudioSourceAccess::status(*audio, reference);
        require(status.at("accepted_triggers") == 4,
                "Capture status did not come from actual runtime requests");
        test::observe_ui = true;
        bool live_view = false;
        auto draw = [&](test::ImGuiSoftwareCapture& target, float height) {
            test::ui_targets.clear();
            target.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({700, height}, ImGuiCond_Always);
            ImGui::Begin("Sound Cue", nullptr, ImGuiWindowFlags_NoResize);
            if (live_view)
                live.draw("offline-capture", reference, status, true, false, "",
                          [&](AudioSourceOperation operation) {
                              AudioSourceAccess::control(*audio, reference, operation);
                              return true;
                          });
            else
                editor.draw(scene, cue, sound_cue_json(scene.entity(cue).get<SoundCue>()),
                            path_text(root));
            ImGui::End();
            target.render();
        };
        auto frame = [&] { draw(*capture, repeating ? 1280 : 1100); };
        auto click = [&](const std::string& key) {
            frame();
            require(test::ui_targets.contains(key), "Cue capture widget is absent: " + key);
            auto target = test::ui_targets.at(key);
            require(target.enabled && target.minimum.y >= target.clip_minimum.y &&
                        target.maximum.y <= target.clip_maximum.y,
                    "Cue capture widget is clipped or disabled: " + key);
            auto& io = ImGui::GetIO();
            for (unsigned i = 0; i < 6; ++i) {
                io.AddMousePosEvent((target.minimum.x + target.maximum.x) * .5f,
                                    (target.minimum.y + target.maximum.y) * .5f);
                frame();
            }
            require(ImGui::GetCurrentContext()->HoveredId == target.id,
                    "Cue capture pointer missed widget: " + key);
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
        };
        frame();
        frame();
        click("sound-cue:edit");
        click("sound-cue:node:0");
        click(timed ? "sound-cue:node:3" : "sound-cue:node:1");
        click("sound-cue:node:4");
        frame();
        frame();
        capture->save(output / "cue-layout-review.png");
        for (const char* key :
             {"sound-cue:seed", "sound-cue:root",
              timed ? "sound-cue:source:3" : "sound-cue:source:1",
              timed ? "sound-cue:delay:3" : "sound-cue:delay:1", "sound-cue:weight:4:0",
              "sound-cue:weight:4:1", "sound-cue:apply", "sound-cue:reset"}) {
            require(test::ui_targets.contains(key), "Cue capture omitted a required control");
            const auto& target = test::ui_targets.at(key);
            require(target.minimum.y >= target.clip_minimum.y &&
                        target.maximum.y <= target.clip_maximum.y,
                    std::string("Cue capture clipped a required control: ") + key);
        }
        if (repeating) {
            require(status.at("repeat_running").get<bool>(),
                    "Repeated capture has no active producer");
            for (const auto* key : {"sound-cue:repeat-mode", "sound-cue:repeat-interval",
                                    "sound-cue:repeat-startup", "sound-cue:repeat-game-pause"}) {
                const auto& target = test::ui_targets.at(key);
                require(target.minimum.y >= target.clip_minimum.y &&
                            target.maximum.y <= target.clip_maximum.y,
                        "Repeating capture clipped a setting");
            }
        }
        capture->save(output / (completion  ? "audio-sound-cue-completion.png"
                                : repeating ? "audio-sound-cue-repeat.png"
                                            : "audio-sound-cue.png"));
        live_view = true;
        // The capture backend owns the current ImGui context. Retire it before
        // constructing another viewport so each SDL renderer has its own fonts.
        capture.reset();
        {
            const float height = completion ? 420 : repeating ? 380 : 260;
            test::ImGuiSoftwareCapture compact(700, int(height));
            ui::style(1);
            draw(compact, height);
            draw(compact, height);
            draw(compact, height);
            draw(compact, height);
            require(ImGui::GetDrawData()->TotalVtxCount > 200,
                    "Live cue capture produced no UI draw data");
            require(test::ui_targets.at("live-sound-cue:trigger").enabled,
                    "Cue live capture has no enabled trigger");
            if (repeating)
                for (const auto* key :
                     {"live-sound-cue:repeat-start", "live-sound-cue:repeat-pause",
                      "live-sound-cue:repeat-stop"}) {
                    const auto& target = test::ui_targets.at(key);
                    require(target.enabled && target.minimum.y >= target.clip_minimum.y &&
                                target.maximum.y <= target.clip_maximum.y,
                            "Repeating capture clipped or disabled a lifecycle control");
                }
            compact.save(output / (completion  ? "audio-sound-cue-completion-live.png"
                                   : repeating ? "audio-sound-cue-repeat-live.png"
                                               : "audio-sound-cue-live.png"));
        }
        require(scene.snapshot() == before && runtime_scene.snapshot() == before,
                "Capture changed authored component values");
        test::observe_ui = false;
    }
    SDL_Quit();
}
