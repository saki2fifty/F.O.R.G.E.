#pragma once
#include "imgui_software_capture.hpp"
#include "widgets.hpp"

void capture_reverb_inspector(const std::filesystem::path& gallery,
                              const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    const auto root = output / "project";
    require(!std::filesystem::exists(root), "Capture project already exists");
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    {
        test::ImGuiSoftwareCapture capture(720, 1400);
        ui::style(1);
        EngineContext engine;
        Scene scene(engine.world());
        SceneDocument project(scene);
        project.open_project(root);
        AssetId clip;
        const auto catalog = AssetCatalog::open_project(root);
        for (const auto& [id, record] : catalog.records())
            if (record.source == "Assets/FalloffTone.wav")
                clip = id;
        require(bool(clip), "Capture's original tone is missing");
        const auto actor = authoring_command(scene, "entity.create", {{"name", "Source reverb"}})
                               .at("selected")
                               .get<std::string>();
        authoring_command(scene, "component.add",
                          {{"entity", actor}, {"component", "forge.audio_source"}});
        authoring_command(scene, "property.set_many",
                          {{"entity", actor},
                           {"component", "forge.audio_source"},
                           {"values",
                            {{"clip", clip},
                             {"spatialized", false},
                             {"bus", 2},
                             {"reverb_enabled", true},
                             {"reverb_decay", .6},
                             {"reverb_damping", .1},
                             {"reverb_wet", .5}}}});
        const auto before = scene.snapshot();
        ui::EditorUiContext owner;
        owner.scene = &scene;
        ui::ContextScope scope(owner);
        ComponentInspector inspector;
        test::observe_ui = true;
        for (unsigned frame = 0; frame < 12; ++frame) {
            test::ui_targets.clear();
            capture.new_frame();
            ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({720, 1400}, ImGuiCond_Always);
            ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoResize);
            inspector.draw(scene, project, actor);
            const auto found =
                test::ui_targets.find("component-field:forge.audio_source:reverb_wet");
            if (found != test::ui_targets.end()) {
                const auto bottom = ImGui::GetCurrentWindow()->InnerClipRect.Max.y - 60;
                if (found->second.maximum.y > bottom)
                    ImGui::SetScrollY(ImGui::GetScrollY() + found->second.maximum.y - bottom);
            }
            ImGui::End();
            capture.render();
        }
        for (const char* field :
             {"reverb_enabled", "reverb_decay", "reverb_damping", "reverb_wet"}) {
            const auto& target =
                test::ui_targets.at(std::string("component-field:forge.audio_source:") + field);
            require(target.enabled && target.minimum.y >= target.clip_minimum.y &&
                        target.maximum.y <= target.clip_maximum.y,
                    "Reverb capture clipped a required control");
        }
        require(scene.snapshot() == before && owner.problems.size() == 0,
                "Inspector capture changed Scene intent or raised a Problem");
        capture.save(output / "audio-reverb.png");
        test::observe_ui = false;
    }
    SDL_Quit();
}
