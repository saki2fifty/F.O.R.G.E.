#include "editor/animation_preview.hpp"
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
        require(argc == 3, "Need prepared project and UI scratch root");
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
            bool play = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, scene, play, io.DeltaTime);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1600, 1600}, ImGuiCond_Always);
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
            auto click = [&](const std::string& name) {
                frame();
                const auto box = target("button:" + name);
                ImVec2 p{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
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
            number("animation_preview:seconds", "0");
            number("animation_preview:weight", "0.5");
            number("animation_preview:playhead", "1");
            auto sampled = editor.snapshot(scene.effective_document());
            Json pose;
            for (const auto& item : sampled.at("entities"))
                if (item.at("id") == actor)
                    pose = item.at("animation_pose");
            require(std::abs(pose.at("blend_weight").get<double>() - .5) < .001,
                    "UI blend target did not apply at scale " + std::to_string(scale) + ": " +
                        pose.at("blend_weight").dump());
            require(std::abs(pose.at("time").get<double>() - 1) < .001, "UI scrub did not seek");
            require(scene.document() == before && scene.revision() == revision,
                    "Preview changed authoring/history");
            click("Apply preview settings to Animator");
            require(!editor.preview_active(), "Applying settings left stale preview active");
            require(scene.entity(actor).get<Animator>().blend_weight == .5f,
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
