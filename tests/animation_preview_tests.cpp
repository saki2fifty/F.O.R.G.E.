#include "editor/animation_preview.hpp"
#include "editor/animation_states_editor.hpp"
#include "editor/component_inspector.hpp"
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3 || argc == 4, "Need prepared project and UI scratch root");
        const bool states_mode = argc == 4 && std::string_view(argv[3]) == "--states";
        const bool plane_mode = argc == 4 && std::string_view(argv[3]) == "--space2d";
        const bool space_mode = plane_mode || (argc == 4 && std::string_view(argv[3]) == "--space");
        require(argc == 3 || states_mode || space_mode, "Unknown preview fixture mode");
        for (float scale : {.65f, 1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
            std::filesystem::create_directories(root);
            std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            std::ifstream input(root / (plane_mode
                                            ? "Scenes/65-two-dimensional-blend-spaces.scene.json"
                                        : space_mode ? "Scenes/43-animation-blend-spaces.scene.json"
                                                     : "main.scene.json"));
            Json source;
            input >> source;
            scene.restore_snapshot(source);
            std::string actor;
            for (const auto& item : source.at("entities"))
                if (item.at("components").contains("forge.animator"))
                    actor = item.at("id");
            require(!actor.empty(), "Prepared fixture lacks Animator");
            const auto before = scene.document();
            const auto revision = scene.revision();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1700, 1700};
            io.DeltaTime = 1.f / 60;
            io.FontGlobalScale = scale;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            context.selection.select_entity(actor);
            test::observe_ui = true;
            AnimationPreviewEditor editor;
            editor.open(project, scene);
            AnimationStatesEditor states_editor;
            bool state_document = false;
            std::string message;
            bool play = false;
            ComponentInspector inspector;
            bool playback_inspector = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, scene, play, io.DeltaTime);
                states_editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1600, 1600}, ImGuiCond_Always);
                if (playback_inspector) {
                    ImGui::Begin("Live Inspector test");
                    const auto effective =
                        Json{{"entities", Json::array({{{"id", actor},
                                                        {"components", Json::object()},
                                                        {"animation_playback",
                                                         {{"state_name", "Walk"},
                                                          {"time", .5},
                                                          {"duration", 2.},
                                                          {"progress", .25},
                                                          {"speed", 1.},
                                                          {"enabled", true},
                                                          {"loop", true},
                                                          {"playing", true},
                                                          {"finished", false},
                                                          {"transitioning", true},
                                                          {"transition_weight", .5},
                                                          {"blend_clip", nullptr}}}}})}};
                    inspector.draw_runtime(
                        effective, {{"components", Json::array()}}, root, actor,
                        [](const auto&, const auto&, const auto&) { return false; });
                    ImGui::End();
                } else if (state_document)
                    states_editor.draw(project, scene, play);
                else
                    editor.draw(project, scene, play);
                ImGui::Render();
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto target = [&](const std::string& name) {
                auto it = test::ui_targets.find(name);
                require(it != test::ui_targets.end() && it->second.enabled,
                        "Missing/disabled UI target " + name);
                return it->second;
            };
            auto click_target = [&](const std::string& name) {
                frame();
                const auto box = target(name);
                ImVec2 p{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto click = [&](const std::string& name) { click_target("button:" + name); };
            auto number = [&](const std::string& name, const char* value) {
                frame();
                auto box = target(name);
                ImVec2 p{box.minimum.x + 80, (box.minimum.y + box.maximum.y) / 2};
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(value);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            playback_inspector = true;
            frame();
            frame();
            const auto progress_box = target("animation_playback:progress");
            require(progress_box.maximum.x > progress_box.minimum.x &&
                        progress_box.maximum.y > progress_box.minimum.y,
                    "Live playback progress has no visible geometry");
            require(scene.document() == before && scene.revision() == revision,
                    "Read-only live status changed authoring");
            playback_inspector = false;
            frame();
            frame();
            const auto selected_row =
                std::find_if(source.at("entities").begin(), source.at("entities").end(),
                             [&](const auto& row) { return row.at("id") == actor; });
            require(
                selected_row != source.at("entities").end() &&
                    test::ui_targets.contains("animation_preview:object:" +
                                              selected_row->at("name").template get<std::string>()),
                "Preview object omitted its authored display name");
            click("Play preview");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!test::ui_targets.contains("animation_preview:playhead")) {
                require(std::chrono::steady_clock::now() < deadline, "Preview resources timed out");
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            click("Pause preview");
            if (states_mode) {
                click_target("animation_preview:state_combo");
                click_target("animation_preview:state:2");
                frame();
            } else if (!space_mode) {
                number("animation_preview:seconds", "0");
                number("animation_preview:weight", "0.5");
            }
            number("animation_preview:playhead", states_mode ? "0.5" : "1");
            if (space_mode)
                number("animation_preview:position", "0.75");
            if (plane_mode)
                number("animation_preview:position_y", "0.25");
            auto sampled = editor.snapshot(scene.effective_document());
            Json pose;
            for (const auto& item : sampled.at("entities"))
                if (item.at("id") == actor)
                    pose = item.at("animation_pose");
            if (states_mode)
                require(pose.at("state") == 2, "UI state selector did not apply");
            else if (plane_mode) {
                require(std::abs(pose.at("blend_position").get<double>() - .75) < .001 &&
                            std::abs(pose.at("blend_position_y").get<double>() - .25) < .001 &&
                            pose.at("blend_samples").size() == 3 &&
                            std::abs(pose.at("morphs")[0].at("weights")[0].get<double>() - .5) <
                                .001,
                        "UI XY controls did not select the expected three-layer pose/morph");
            } else if (space_mode) {
                require(std::abs(pose.at("blend_position").get<double>() - .75) < .001 &&
                            std::abs(pose.at("blend_weight").get<double>() - .75) < .001 &&
                            std::abs(pose.at("duration").get<double>() - 2) < .001,
                        "UI position did not select phase-aligned neighboring samples");
            } else
                require(std::abs(pose.at("blend_weight").get<double>() - .5) < .001,
                        "UI blend target did not apply at scale " + std::to_string(scale) + ": " +
                            pose.at("blend_weight").dump());
            require(std::abs(pose.at("time").get<double>() - (states_mode ? .5 : 1)) < .001,
                    "UI scrub did not seek: " + pose.at("time").dump());
            require(scene.document() == before && scene.revision() == revision,
                    "Preview changed authoring/history");
            click("Apply preview settings to Animator");
            require(!editor.preview_active(), "Applying settings left stale preview active");
            require(states_mode  ? scene.entity(actor).get<Animator>().state == 2
                    : space_mode ? scene.entity(actor).get<Animator>().blend_position == .75f
                                 : scene.entity(actor).get<Animator>().blend_weight == .5f,
                    "Preview settings not saved to Animator");
            if (plane_mode)
                require(scene.entity(actor).get<Animator>().blend_position_y == .25f,
                        "Preview Apply lost the second axis");
            require(scene.undo() && scene.document() == before,
                    "Settings were not one atomic Undo step");
            require(scene.redo(), "Settings Redo failed");
            editor.poll(project, scene, false, io.DeltaTime);
            click("Play preview");
            click("Stop preview");
            require(!editor.preview_active(), "Stop left temporary preview active");
            click("Play preview");
            play = true;
            frame();
            require(!editor.preview_active(), "Entering Play did not stop preview");
            play = false;
            frame();
            click("Play preview");
            editor.close();
            require(!editor.is_open() && !editor.preview_active(), "Close did not release preview");
            if (states_mode) {
                states_editor.open(project, "Assets/Test.states.json");
                state_document = true;
                frame();
                frame();
                require(states_editor.is_open(), "State source editor did not open");
                const auto original = states_editor.document()->source().document;
                click_target("animation_states:state:2");
                click_target("animation_states:exit_enabled");
                click_target("animation_states:exit_target");
                click_target("animation_states:exit_choice:3");
                number("animation_states:exit_phase", "0.625");
                require(states_editor.document()->source().compiled().state(2).exit_state == 3 &&
                            states_editor.document()->source().compiled().state(2).exit_phase ==
                                .625f,
                        "Exit target/phase UI failed");
                click("Undo");
                click("Undo");
                click("Undo");
                require(states_editor.document()->source().document == original,
                        "Exit UI Undo changed the original source");
                number("animation_states:fade", "0.75");
                require(states_editor.document()->source().compiled().state(2).transition_seconds ==
                            .75f,
                        "State fade UI failed");
                number("animation_states:name", "Walking");
                require(states_editor.document()->source().compiled().state(2).name == "Walking",
                        "State rename UI failed");
                click("Move up");
                require(states_editor.document()->source().document.at("states")[0].at("id") == 2,
                        "UI reorder changed ID");
                click("Undo");
                click("Undo");
                click("Undo");
                require(states_editor.document()->source().document == original,
                        "State source Undo failed");
                click("Redo");
                click("Redo");
                require(states_editor.document()->source().compiled().state(2).transition_seconds ==
                            .75f,
                        "State source Redo failed");
                click_target("animation_states:add_marker");
                number("animation_states:marker_name", "FootLeft");
                number("animation_states:marker_phase", "0.25");
                number("animation_states:marker_value", "0.75");
                auto marker = states_editor.document()->source().compiled().state(2).markers.at(0);
                require(marker.id == 1 && marker.name == "FootLeft" && marker.phase == .25 &&
                            marker.value == .75,
                        "Marker fields did not commit through actual UI");
                click("Undo");
                click("Undo");
                click("Undo");
                click("Redo");
                click("Redo");
                click("Redo");
                click_target("animation_states:remove_marker");
                require(states_editor.document()->source().compiled().state(2).markers.empty(),
                        "Remove marker failed");
                click("Undo");
                require(states_editor.document()->source().compiled().state(2).markers.at(0).id ==
                            1,
                        "Marker Undo changed identity");
                click("Redo");
                click_target("animation_states:add_marker");
                require(states_editor.document()->source().compiled().state(2).markers.at(0).id ==
                                2 &&
                            states_editor.document()->source().document.at("next_marker_id") == 3,
                        "Marker ID was recycled by editor history");
                states_editor.request_close();
                frame();
                frame();
                click("Keep editing");
                require(states_editor.is_open() && states_editor.close_cancelled,
                        "State source close cancellation failed");
                click("Save / Publish");
                const auto publish_deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (states_editor.dirty()) {
                    require(std::chrono::steady_clock::now() < publish_deadline,
                            "UI state publication timed out");
                    frame();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                require(bool(states_editor.take_catalog()),
                        "UI publication did not provide the selected catalog");
                const auto published_source = states_editor.document()->source().document;
                // Attaching a set must also work from a valid manual two-clip Animator.
                authoring_command(scene, "property.set",
                                  {{"entity", actor},
                                   {"component", "forge.animator"},
                                   {"field", "states"},
                                   {"value", nullptr}});
                authoring_command(scene, "property.set",
                                  {{"entity", actor},
                                   {"component", "forge.animator"},
                                   {"field", "blend_clip"},
                                   {"value", published_source.at("states")[1].at("clip")}});
                authoring_command(scene, "property.set",
                                  {{"entity", actor},
                                   {"component", "forge.animator"},
                                   {"field", "blend_weight"},
                                   {"value", .5}});
                authoring_command(scene, "property.set",
                                  {{"entity", actor},
                                   {"component", "forge.animator"},
                                   {"field", "blend_mask"},
                                   {"value", AssetId::generate()}});
                authoring_command(scene, "property.set",
                                  {{"entity", actor},
                                   {"component", "forge.animator"},
                                   {"field", "blend_space"},
                                   {"value", AssetId::generate()}});
                const auto before_attach = scene.document();
                click("Attach to selected Animator");
                require(scene.entity(actor).get<Animator>().state == 0 &&
                            !scene.entity(actor).get<Animator>().blend_mask.id &&
                            !scene.entity(actor).get<Animator>().blend_space.id,
                        "UI state attachment did not select entry and clear mask/space modes");
                require(scene.undo() && scene.document() == before_attach,
                        "State attachment was not one Scene Undo step");
                number("animation_states:fade", "0.5");
                states_editor.request_close();
                frame();
                frame();
                click("Discard");
                require(!states_editor.is_open(), "State source discard did not close");
                require(AnimationStatesDocument(project.writer_guard(), "Assets/Test.states.json")
                                .source()
                                .document == published_source,
                        "Discard changed source on disk");
                auto invalid_source = published_source;
                invalid_source["states"][1]["id"] = invalid_source["states"][0]["id"];
                atomic_write(root / "Assets/Invalid.states.json", invalid_source.dump());
                states_editor.open(project, "Assets/Invalid.states.json");
                frame();
                frame();
                require(states_editor.is_open() && !states_editor.document(),
                        "Invalid state source failed silently");
                click("Close");
                require(!states_editor.is_open(), "Failed source error document did not close");
            }
            test::observe_ui = false;
            ImGui::DestroyContext();
        }
        std::cout << "animation preview scrub, blend, atomic apply, restoration and lifecycle "
                     "passed at 65/100/200 percent\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
