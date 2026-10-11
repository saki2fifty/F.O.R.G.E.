#include "editor/animation_preview.hpp"
#include "editor/animation_states_editor.hpp"
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
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
            std::filesystem::create_directories(root);
            std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            std::ifstream input(root / "main.scene.json");
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
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, scene, play, io.DeltaTime);
                states_editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1600, 1600}, ImGuiCond_Always);
                if (state_document)
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
            frame();
            frame();
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
            } else {
                number("animation_preview:seconds", "0");
                number("animation_preview:weight", "0.5");
            }
            number("animation_preview:playhead", states_mode ? "0.5" : "1");
            auto sampled = editor.snapshot(scene.effective_document());
            Json pose;
            for (const auto& item : sampled.at("entities"))
                if (item.at("id") == actor)
                    pose = item.at("animation_pose");
            if (states_mode)
                require(pose.at("state") == 2, "UI state selector did not apply");
            else
                require(std::abs(pose.at("blend_weight").get<double>() - .5) < .001,
                        "UI blend target did not apply at scale " + std::to_string(scale) + ": " +
                            pose.at("blend_weight").dump());
            require(std::abs(pose.at("time").get<double>() - (states_mode ? .5 : 1)) < .001,
                    "UI scrub did not seek: " + pose.at("time").dump());
            require(scene.document() == before && scene.revision() == revision,
                    "Preview changed authoring/history");
            click("Apply preview settings to Animator");
            require(!editor.preview_active(), "Applying settings left stale preview active");
            require(states_mode ? scene.entity(actor).get<Animator>().state == 2
                                : scene.entity(actor).get<Animator>().blend_weight == .5f,
                    "Preview settings not saved to Animator");
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
                const auto before_attach = scene.document();
                click("Attach to selected Animator");
                require(scene.entity(actor).get<Animator>().state == 0,
                        "UI state attachment did not select entry");
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
