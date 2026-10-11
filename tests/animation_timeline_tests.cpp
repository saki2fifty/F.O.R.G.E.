#include "animation_timeline.hpp"
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
        require(argc == 2, "Need timeline scratch root");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto actor = authoring_command(scene, "entity.create", {{"name", "Preview cube"}})
                                   .at("selected")
                                   .get<std::string>();
            auto source = TransformAnimationDocument::create(project.writer_guard(),
                                                             "Assets/Motion.animation.json");
            const auto id = source->source().asset();
            source.reset();
            const auto before = scene.document();
            const auto revision = scene.revision();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1400, 1400};
            io.DeltaTime = 1.f / 60;
            io.FontGlobalScale = scale;
            ui::interface_scale = scale;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            AnimationTimelineEditor editor;
            editor.open(project, "Assets/Motion.animation.json");
            editor.bind(scene, actor);
            std::string message;
            bool play = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, scene, play, io.DeltaTime, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1350, 1350}, ImGuiCond_Always);
                editor.draw(project, scene, play);
                ImGui::Render();
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto target = [&](const std::string& name) {
                const auto at = test::ui_targets.find(name);
                require(at != test::ui_targets.end(), "Missing widget " + name);
                require(at->second.enabled, "Disabled widget " + name);
                auto box = at->second;
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Animation Timeline");
                    require(window != nullptr, "Timeline window missing");
                    ImGui::SetScrollY(window, window->Scroll.y +
                                                  (box.minimum.y + box.maximum.y) / 2 -
                                                  (box.clip_minimum.y + box.clip_maximum.y) / 2);
                    frame();
                    frame();
                    box = test::ui_targets.at(name);
                }
                require(box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y,
                        "Clipped widget " + name);
                return box;
            };
            auto click = [&](const std::string& name) {
                frame(); // Allow an appearing auto-sized popup to settle before input.
                const auto box = target("button:" + name);
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto ruler_point = [&](double fraction) {
                const auto box = target("animation:ruler");
                return ImVec2{box.minimum.x + float(fraction) * (box.maximum.x - box.minimum.x),
                              box.minimum.y + 50};
            };
            auto scrub = [&](double fraction) {
                const auto p = ruler_point(fraction);
                const auto ruler = target("animation:ruler");
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                // ImGui floors pointer coordinates; use the performed scrub time.
                return std::clamp(double(std::floor(p.x) - ruler.minimum.x) /
                                      (ruler.maximum.x - ruler.minimum.x),
                                  0., 1.) *
                       editor.document()->source().clip().duration;
            };
            frame();
            frame();
            scrub(.5);
            const auto box = target("animation:position");
            const ImVec2 p{box.minimum.x + 20, (box.minimum.y + box.maximum.y) / 2};
            mouse(p, false);
            mouse(p, true);
            mouse(p, false);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
            io.AddInputCharactersUTF8("6");
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            frame();
            click("Add / Update key at playhead");
            require(editor.document()->source().clip().keys.size() == 2,
                    "Add-key widget moved the existing key");
            require(std::abs(editor.document()->source().clip().keys[1].pose.translation.x - 6) <
                        .001,
                    "Position widget did not author the key");
            const auto authored = editor.document()->source().document;
            click("Undo");
            require(editor.document()->source().clip().keys.size() == 1,
                    "Timeline Undo missed the key edit");
            click("Redo");
            require(editor.document()->source().document == authored,
                    "Timeline Redo changed key identity");
            const auto key = editor.document()->source().clip().keys.front().id;
            auto start = ruler_point(0), finish = ruler_point(.25);
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            mouse(finish, false);
            require(editor.document()->source().clip().keys.front().id == key &&
                        std::abs(editor.document()->source().clip().keys.front().time - 500000) <=
                            2000,
                    "Ruler drag did not retime the stable key: scale=" + std::to_string(scale) +
                        " time=" +
                        std::to_string(editor.document()->source().clip().keys.front().time));
            auto retimed = editor.document()->source().document;
            start = ruler_point(.25);
            finish = ruler_point(.4);
            mouse(start, false);
            mouse(start, true);
            mouse(finish, true);
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            mouse(finish, false);
            require(editor.document()->source().document == retimed,
                    "Cancelled ruler drag changed source");
            auto widget_click = [&](const std::string& name) {
                frame();
                const auto box = target(name);
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            widget_click("animation:curve_section");
            widget_click("animation:curve_mode");
            widget_click("animation:curve_choice:1");
            click("Apply curve settings");
            require(editor.document()->source().document.at("version") == 2 &&
                        editor.document()->source().clip().keys.front().curves.cubic,
                    "Cubic choice did not author the selected key");
            const auto curved = editor.document()->source().document;
            click("Undo");
            require(editor.document()->source().document == retimed,
                    "Curve apply was not one Undo step");
            click("Redo");
            require(editor.document()->source().document == curved,
                    "Curve Redo lost key identity/slopes");
            retimed = curved;
            auto slope = [&](const char* text) {
                frame();
                const auto box = target("animation:curve_translation_out");
                const ImVec2 p{box.minimum.x + 25, (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(text);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            const auto safe_revision = editor.document()->revision();
            slope("3000000000");
            click("Apply curve settings");
            require(editor.document()->revision() == safe_revision &&
                        editor.document()->source().document == curved,
                    "Invalid curve edit changed source/history");
            slope("0");
            click("Apply curve settings");
            const auto canvas = target("animation:curve_canvas");
            require(canvas.maximum.y > canvas.minimum.y && canvas.maximum.x > canvas.minimum.x,
                    "Curve view is empty");
            const auto curve_time = scrub(.3125);
            const auto keys = editor.document()->source().clip().keys;
            const auto phase = (curve_time - keys[0].time) / (keys[1].time - keys[0].time);
            const auto expected_ease = 6 * phase * phase * (3 - 2 * phase);
            const auto eased = editor.preview(scene.effective_document());
            for (const auto& row : eased.at("entities"))
                if (row.at("id") == actor)
                    require(std::abs(row.at("components")
                                         .at("forge.local_translation")
                                         .at("x")
                                         .get<double>() -
                                     expected_ease) < .001,
                            "Curve preview differs: scale=" + std::to_string(scale) +
                                " expected=" + std::to_string(expected_ease) + " actual=" +
                                row.at("components").at("forge.local_translation").at("x").dump());
            scrub(.375);
            require(editor.preview_active(), "Scrubbing did not enable preview");
            const auto preview = editor.preview(scene.effective_document());
            require(preview != scene.effective_document(), "Scrubbing omitted render substitution");
            require(scene.document() == before && scene.revision() == revision,
                    "Timeline preview changed Scene source/history");
            click("Stop preview");
            require(!editor.preview_active(), "Stop did not restore authored view");
            click("Play preview");
            require(editor.preview_active(), "Playback control did not begin preview");
            play = true;
            frame();
            require(!editor.preview_active(), "Play did not stop timeline preview");
            play = false;
            frame();
            click("Save & Publish");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                frame();
            }
            require(!editor.pending() && !editor.dirty(), "Timeline save/publication failed");
            frame();
            click("Attach animation to object");
            require(scene.entity(actor).get<TransformAnimator>().clip.id == id,
                    "Attachment used wrong clip");
            require(scene.undo() && scene.document() == before,
                    "Attachment was not one atomic Scene Undo step");
            scene.redo();
            editor.request_close();
            frame();
            require(!editor.is_open(), "Clean timeline did not close");
            editor.open(project, "Assets/Motion.animation.json");
            editor.bind(scene, actor);
            frame();
            require(editor.document()->source().document == retimed,
                    "Timeline reopen lost authored keys");
            scrub(.7);
            click("Add / Update key at playhead");
            editor.request_close();
            frame();
            require(ImGui::GetTopMostPopupModal(), "Dirty timeline closed without draft guard");
            click("Keep editing");
            require(editor.is_open() && editor.close_cancelled,
                    "Keep editing did not cancel close");
            editor.request_close();
            frame();
            click("Discard");
            frame();
            require(!editor.is_open(), "Discard retained dirty timeline");
            require(!ImGui::GetTopMostPopupModal(), "Timeline close left a stale modal");
            editor.open(project, "Assets/Motion.animation.json");
            editor.bind(scene, actor);
            frame();
            scrub(.7);
            click("Add / Update key at playhead");
            editor.request_close();
            frame();
            click("Save");
            const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.is_open() && std::chrono::steady_clock::now() < close_deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                frame();
            }
            require(!editor.is_open() && !ImGui::GetTopMostPopupModal(),
                    "Save before close retained a stale document/modal");
            test::observe_ui = false;
            ImGui::DestroyContext();
        }
        ui::interface_scale = 1;
        std::cout << "timeline key editing, drag/cancel, preview restoration, publication, "
                     "attachment and draft guards passed at 65/100/200 percent\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
