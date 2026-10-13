#include "editor/animation_timeline.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Need Gallery and curve UI scratch root");
        for (const float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
            std::filesystem::create_directories(root);
            std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root);
            const auto actor =
                authoring_command(scene, "entity.create", {{"name", "Curve preview"}})
                    .at("selected")
                    .get<std::string>();
            const auto authored = scene.document();
            const auto scene_revision = scene.revision();
            AnimationTimelineEditor editor;
            const std::filesystem::path path = "Assets/EasedPlatform.animation.json";
            editor.open(project, path);
            editor.bind(scene, actor);
            require(editor.document() && !editor.dirty(), "Gallery curve did not open cleanly");
            const auto original = editor.document()->source().document;
            const auto keys = editor.document()->source().clip().keys;
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2500, 2700};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::style(scale);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            std::string message;
            bool locked = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, scene, locked, io.DeltaTime, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({2400, 2550}, ImGuiCond_Always);
                editor.draw(project, scene, locked);
                ImGui::Render();
            };
            auto target = [&](const std::string& id) {
                require(test::ui_targets.contains(id), "Missing curve control " + id);
                auto box = test::ui_targets.at(id);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Animation Timeline");
                    require(window != nullptr, "Timeline window missing");
                    ImGui::SetScrollY(window, window->Scroll.y +
                                                  (box.minimum.y + box.maximum.y) / 2 -
                                                  (box.clip_minimum.y + box.clip_maximum.y) / 2);
                    frame();
                    frame();
                    require(test::ui_targets.contains(id),
                            "Curve control disappeared while scrolling " + id);
                    box = test::ui_targets.at(id);
                }
                const ImVec2 center{(box.minimum.x + box.maximum.x) / 2,
                                    (box.minimum.y + box.maximum.y) / 2};
                require(box.enabled && center.x >= box.clip_minimum.x &&
                            center.x <= box.clip_maximum.x && center.y >= box.clip_minimum.y &&
                            center.y <= box.clip_maximum.y,
                        "Clipped/disabled curve control " + id + " at " + std::to_string(scale));
                return center;
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto click = [&](const std::string& id) {
                frame();
                const auto p = target(id);
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto slope = [&](const char* value) {
                frame();
                target("animation:curve_translation_out");
                const auto box = test::ui_targets.at("animation:curve_translation_out");
                const ImVec2 p{box.minimum.x + 25 * scale, (box.minimum.y + box.maximum.y) / 2};
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
                io.AddInputCharactersUTF8(value);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            frame();
            frame();
            click("animation:curve_section");
            click("button:Linear tangents");
            require(editor.document()->source().clip().keys[0].curves.outgoing.translation[0] == 3,
                    "Linear preset did not use the neighboring motion");
            editor.undo();
            frame();
            require(editor.document()->source().document == original, "Preset Undo failed");
            click("button:Smooth tangents");
            require(editor.document()->source().clip().keys[0].curves.outgoing.translation[0] == 3,
                    "Smooth preset endpoint differs");
            editor.undo();
            frame();
            click("button:Select all keys");
            click("button:Linear tangents");
            require(std::abs(editor.document()->source().clip().sample(500000).translation.x +
                             1.5) < 1e-6,
                    "Group Linear preset did not produce linear motion");
            const auto linear = editor.document()->source().document;
            click("button:Flat tangents");
            require(editor.document()->source().clip().keys[1].curves.incoming.translation[0] == 0,
                    "Group Flat preset did not clear slopes");
            editor.undo();
            frame();
            require(editor.document()->source().document == linear,
                    "Flat preset was not one Undo step");
            editor.undo();
            frame();
            require(editor.document()->source().document == original, "Group preset Undo failed");
            click("animation:key:" + keys[0].id.str());
            // Selection must apply valid pending fields without invalidating list iteration.
            slope("1.25");
            click("animation:key:" + keys[1].id.str());
            require(editor.document()->source().clip().keys[0].curves.outgoing.translation[0] ==
                        1.25,
                    "Changing key lost pending slopes");
            editor.undo();
            frame();
            click("animation:key:" + keys[0].id.str());
            slope("1.75");
            click("animation:key:" + keys[0].id.str());
            require(editor.document()->source().clip().keys[0].curves.outgoing.translation[0] ==
                        1.75,
                    "Reselecting the active key lost pending slopes");
            editor.undo();
            frame();
            slope("2.25");
            click("animation:playhead");
            require(editor.document()->source().clip().keys[0].curves.outgoing.translation[0] ==
                        2.25,
                    "Playhead scrub orphaned pending slopes");
            editor.undo();
            frame();
            click("animation:key:" + keys[0].id.str());
            click("button:Fit curve and handles");
            const auto start = target("animation:tangent_out");
            const auto revision = editor.document()->revision();
            mouse(start, false);
            mouse(start, true);
            const ImVec2 moved{start.x, start.y - 35 * scale};
            mouse(moved, true);
            frame();
            require(editor.dirty() && editor.document()->revision() == revision,
                    "Tangent drag hid dirty intent or committed before release");
            require(editor.document()->source().document == original,
                    "Held handle replaced applied source");
            mouse(moved, false);
            frame();
            require(editor.document()->revision() == revision + 1 &&
                        editor.document()->source().clip().keys[0].curves.outgoing.translation[0] >
                            0,
                    "Outgoing handle did not commit one source history step");
            const auto dragged = editor.document()->source().document;
            require(dragged["keys"][0]["time_us"] == original["keys"][0]["time_us"] &&
                        dragged["keys"][0]["translation"] == original["keys"][0]["translation"] &&
                        dragged["keys"][0]["curves"]["outgoing"]["scale"] ==
                            original["keys"][0]["curves"]["outgoing"]["scale"],
                    "Tangent drag changed a key time/value or another vector channel");
            editor.undo();
            frame();
            require(editor.document()->source().document == original, "Tangent Undo failed");
            editor.redo();
            frame();
            require(editor.document()->source().document == dragged, "Tangent Redo failed");
            editor.undo();
            frame();
            const auto cancel_start = target("animation:tangent_out");
            mouse(cancel_start, false);
            mouse(cancel_start, true);
            mouse({cancel_start.x, cancel_start.y - 30}, true);
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            frame();
            mouse(cancel_start, false);
            frame();
            require(editor.document()->source().document == original,
                    "Escape committed a tangent draft");
            const auto focus_start = target("animation:tangent_out");
            mouse(focus_start, false);
            mouse(focus_start, true);
            mouse({focus_start.x, focus_start.y - 30}, true);
            io.AddFocusEvent(false);
            frame();
            io.AddFocusEvent(true);
            frame();
            mouse(focus_start, false);
            require(editor.document()->source().document == original,
                    "Focus loss committed a tangent draft");
            click("animation:key:" + keys[1].id.str());
            click("button:Fit curve and handles");
            const auto incoming = target("animation:tangent_in");
            mouse(incoming, false);
            mouse(incoming, true);
            mouse({incoming.x, incoming.y - 25 * scale}, false);
            frame();
            require(editor.document()->source().clip().keys[1].curves.incoming.translation[0] < 0,
                    "Incoming handle missed the release-only pointer update");
            editor.undo();
            frame();
            click("animation:curve_channel");
            frame();
            frame();
            click("animation:curve_channel_choice:1");
            click("button:Fit curve and handles");
            const auto scale_handle = target("animation:tangent_out");
            mouse(scale_handle, false);
            mouse(scale_handle, true);
            mouse({scale_handle.x, scale_handle.y - 25 * scale}, false);
            frame();
            require(editor.document()->source().clip().keys[1].curves.outgoing.scale[0] > 0 &&
                        editor.document()->source().clip().keys[1].curves.outgoing.translation[0] ==
                            0,
                    "Scale handle changed the wrong vector channel");
            editor.undo();
            frame();
            click("animation:curve_channel");
            frame();
            frame();
            click("animation:curve_channel_choice:0");
            click("animation:key:" + keys[0].id.str());
            slope("1.5");
            const auto pending_revision = editor.document()->revision();
            require(editor.dirty() && editor.document()->source().document == original,
                    "Pending numeric slope lost dirty tracking or applied early");
            const auto pending_handle = target("animation:tangent_out");
            mouse(pending_handle, false);
            mouse(pending_handle, true);
            mouse({pending_handle.x, pending_handle.y - 25}, true);
            editor.request_close();
            mouse(pending_handle, false);
            frame();
            frame();
            click("button:Keep editing");
            require(editor.is_open() && editor.dirty() &&
                        editor.document()->revision() == pending_revision,
                    "Close/cancel changed source or discarded prior pending numeric fields");
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty() && bool(editor.take_catalog()) &&
                        editor.document()->source().clip().keys[0].curves.outgoing.translation[0] ==
                            1.5,
                    "Save did not apply the preserved pending curve settings");
            const auto saved = editor.document()->source().document;
            target("animation:ruler");
            const auto ruler = test::ui_targets.at("animation:ruler");
            const ImVec2 scrub{ruler.minimum.x + (ruler.maximum.x - ruler.minimum.x) / 4,
                               (ruler.minimum.y + ruler.maximum.y) / 2};
            mouse(scrub, false);
            mouse(scrub, true);
            mouse(scrub, false);
            const auto preview = editor.preview(scene.effective_document());
            bool observed_preview = false;
            for (const auto& row : preview.at("entities"))
                if (row.at("id") == actor) {
                    require(std::abs(row.at("components")
                                         .at("forge.local_translation")
                                         .at("x")
                                         .get<double>() -
                                     .375) < .02,
                            "Actual preview does not evaluate the saved tangent");
                    observed_preview = true;
                }
            require(observed_preview, "Preview target was absent");
            click("animation:key:" + keys[0].id.str());
            const auto saved_bytes =
                asset_detail::read_bytes(root / path, transform_animation_byte_limit);
            slope("3000000000");
            click("animation:key:" + keys[1].id.str());
            click("animation:playhead");
            editor.request_save();
            frame();
            require(editor.dirty() && !editor.pending() &&
                        editor.document()->source().document == saved &&
                        asset_detail::read_bytes(root / path, transform_animation_byte_limit) ==
                            saved_bytes,
                    "Failed pending-curve Save displaced good source");
            editor.undo();
            frame();
            require(!editor.dirty(), "Undo did not cancel invalid pending fields");
            require(scene.document() == authored && scene.revision() == scene_revision,
                    "Curve authoring changed Scene history");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Clean curve document failed to close");
            editor.open(project, path);
            frame();
            require(editor.document()->source().document == saved,
                    "Reopen lost authored curve settings");
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual curve UI at100/150/200%: tangent/preset/fit, release/Undo/cancel, "
                     "channel isolation, pending-close/Save/rollback and reopen passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
