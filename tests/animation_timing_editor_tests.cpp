#include "animation_timeline.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need animation timing UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto actor =
                authoring_command(scene, "entity.create", {{"name", "Timing preview"}})
                    .at("selected")
                    .get<std::string>();
            const auto authored_scene = scene.document();
            auto source = TransformAnimationSource::create(AssetId::generate());
            const auto first = source.clip().keys.front().id;
            const auto middle = source.set_key(520000, {{1.04, 0, 0}, {}, {1.52, 1, 1}});
            const auto last = source.set_key(2 * animation_timebase, {{4, 0, 0}, {}, {3, 1, 1}});
            source.set_tangent_preset(std::array{first, middle, last},
                                      TransformAnimationTangentPreset::Linear);
            for (auto& key : source.document["keys"])
                key["opaque"] = {{"keep", 73}};
            atomic_write(root / "Assets/Timing.animation.json", source.document.dump(2));
            const auto original = source.document;
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2500, 2000};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            io.FontGlobalScale = scale;
            ImGui::GetStyle().ScaleAllSizes(scale);
            unsigned char* pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            ui::interface_scale = scale;
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            AnimationTimelineEditor editor;
            editor.open(project, "Assets/Timing.animation.json");
            editor.bind(scene, actor);
            bool locked = false;
            std::string message;
            auto frame = [&] {
                editor.poll(project, scene, locked, 1.0 / 60, message);
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({2300, 1800}, ImGuiCond_Always);
                editor.draw(project, scene, locked);
                ImGui::Render();
            };
            auto target = [&](const std::string& name) {
                frame();
                check(test::ui_targets.contains(name), "Missing timing UI " + name);
                auto box = test::ui_targets.at(name);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Animation Timeline");
                    check(window, "Timeline window missing");
                    ImGui::SetScrollY(window,
                                      window->Scroll.y + (box.minimum.y + box.maximum.y -
                                                          box.clip_minimum.y - box.clip_maximum.y) *
                                                             .5f);
                    frame();
                    frame();
                    box = test::ui_targets.at(name);
                }
                check(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                          box.maximum.y <= box.clip_maximum.y,
                      "Disabled/clipped timing UI " + name);
                return box;
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto click = [&](const std::string& name) {
                const auto box = target(name);
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto number = [&](const std::string& name, const char* text) {
                const auto box = target(name);
                const ImVec2 p{box.minimum.x + 50 * scale, (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(text);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            frame();
            frame();
            const auto before_revision = editor.document()->revision();
            number("animation:retimed_duration", "4");
            check(editor.document()->revision() == before_revision,
                  "Timing field changed source before Apply");
            click("animation:retime");
            const auto retimed = editor.document()->source().document;
            const auto clip = editor.document()->source().clip();
            check(editor.document()->revision() == before_revision + 1 &&
                      clip.duration == 4000000 && clip.keys[1].id == middle &&
                      clip.keys[1].time == 1040000 &&
                      std::abs(clip.keys[1].curves.outgoing.translation[0] - 1) < 1e-9,
                  "Actual retime times/slopes/history differ");
            editor.undo();
            frame();
            check(editor.document()->source().document == original, "Retime UI Undo differs");
            editor.redo();
            frame();
            check(editor.document()->source().document == retimed, "Retime UI Redo differs");
            const auto no_op_revision = editor.document()->revision();
            click("animation:retime");
            check(editor.document()->revision() == no_op_revision, "No-op retime added history");
            number("animation:retimed_duration", "0");
            click("animation:retime");
            check(editor.document()->source().document == retimed, "Invalid retime changed source");
            click("button:Select all keys");
            number("animation:frame_grid", "2");
            click("animation:snap_frames");
            const auto snapped = editor.document()->source().document;
            const auto snapped_clip = editor.document()->source().clip();
            check(snapped_clip.keys[1].time == 1000000 && snapped_clip.keys[1].id == middle &&
                      snapped["keys"][1]["curves"] == retimed["keys"][1]["curves"],
                  "Actual frame grid changed wrong key fields");
            editor.undo();
            frame();
            check(editor.document()->source().document == retimed, "Frame grid UI Undo differs");
            editor.redo();
            frame();
            check(editor.document()->source().document == snapped, "Frame grid UI Redo differs");
            const auto aligned_revision = editor.document()->revision();
            click("animation:snap_frames");
            check(editor.document()->revision() == aligned_revision,
                  "Aligned frame grid added history");
            number("animation:frame_grid", "0");
            click("animation:snap_frames");
            check(editor.document()->source().document == snapped, "Invalid FPS changed source");
            number("animation:frame_grid", "2");
            locked = true;
            frame();
            check(!test::ui_targets.at("animation:retime").enabled &&
                      !test::ui_targets.at("animation:snap_frames").enabled,
                  "Readonly timing controls enabled");
            locked = false;
            frame();
            editor.request_save();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            do {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while ((editor.pending() || editor.dirty()) &&
                     std::chrono::steady_clock::now() < deadline);
            check(!editor.pending() && !editor.dirty(), "Timing Save/Publish stalled");
            TransformAnimationDocument reopened(project.writer_guard(),
                                                "Assets/Timing.animation.json");
            check(reopened.source().document == snapped && scene.document() == authored_scene,
                  "Timing save/reopen changed source or authored Scene");
            check(AssetCatalog::open_project(root).records().contains(source.asset()),
                  "Timing clip not published");
            ImGui::DestroyContext();
        }
        std::cout << "Actual timing "
                     "fields/history/no-op/rejection/readonly/publication100/150/200% passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
