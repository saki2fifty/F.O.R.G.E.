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
        check(argc == 2, "Need animation value UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto actor =
                authoring_command(scene, "entity.create", {{"name", "Value preview"}})
                    .at("selected")
                    .get<std::string>();
            const auto authored_scene = scene.document();
            auto source = TransformAnimationSource::create(AssetId::generate());
            const auto first = source.clip().keys.front().id;
            TransformAnimationCurves curve;
            curve.cubic = true;
            curve.incoming.translation = curve.outgoing.translation = {2, 0, 0};
            curve.incoming.scale = curve.outgoing.scale = {1, 0, 0};
            source.set_key(0, {{1, 2, -3}, {}, {1, 1, 1}}, false, first, curve);
            const auto middle =
                source.set_key(animation_timebase, {{3, 2, -3}, {}, {2, 1, 1}}, false, {}, curve);
            source.set_key(2 * animation_timebase, {{5, 2, -3}, {}, {3, 1, 1}}, false, {}, curve);
            for (auto& key : source.document["keys"])
                key["opaque"] = {{"keep", 77}};
            atomic_write(root / "Assets/Values.animation.json", source.document.dump(2));
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
            editor.open(project, "Assets/Values.animation.json");
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
                check(test::ui_targets.contains(name), "Missing value UI " + name);
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
                      "Disabled/clipped value UI " + name);
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
            auto number = [&](const std::string& name, unsigned axis, const char* text) {
                const auto box = target(name);
                auto* window = ImGui::FindWindowByName("###Animation Timeline");
                const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                const float width = (window->DC.ItemWidthDefault - 2 * spacing) / 3;
                const ImVec2 p{box.minimum.x + axis * (width + spacing) + 35 * scale,
                               (box.minimum.y + box.maximum.y) / 2};
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
            click("button:Clear key selection");
            check(!test::ui_targets.at("animation:transform_values").enabled &&
                      test::ui_targets.at("animation:reset_values").enabled,
                  "Empty selection permits Apply or disables Reset");
            click("animation:key:" + first.str());
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            frame();
            click("animation:key:" + middle.str());
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            check(editor.selected_keys() == std::set<AssetId>{first, middle},
                  "Actual key selection differs");
            const auto before_revision = editor.document()->revision();
            const std::array<const char*, 3> pm{"-2", "0.5", "0"}, po{"3", "-1", "2"},
                sm{"1.5", "-2", "0.5"};
            for (unsigned axis = 0; axis < 3; ++axis) {
                number("animation:value_position_multiplier", axis, pm[axis]);
                number("animation:value_position_offset", axis, po[axis]);
                number("animation:value_scale_multiplier", axis, sm[axis]);
            }
            check(editor.document()->revision() == before_revision &&
                      editor.document()->source().document == original,
                  "Value fields changed source before Apply");
            click("animation:transform_values");
            const auto adjusted = editor.document()->source().document;
            check(editor.document()->revision() == before_revision + 1 &&
                      adjusted["keys"][2] == original["keys"][2] &&
                      adjusted["keys"][0]["translation"] == Json({1, 0, 2}) &&
                      adjusted["keys"][1]["translation"] == Json({-3, 0, 2}) &&
                      adjusted["keys"][1]["scale"] == Json({3, -2, .5}) &&
                      adjusted["keys"][0]["curves"]["outgoing"]["translation"] == Json({-4, 0, 0}),
                  "Actual selected value edit/slopes/history differ: " + adjusted.dump());
            click("button:Add / Update key at playhead");
            check(editor.document()->revision() == before_revision + 1 &&
                      editor.document()->source().document == adjusted,
                  "Active pose/curve fields were stale after value Apply");
            editor.undo();
            frame();
            check(editor.document()->source().document == original && !editor.can_undo(),
                  "Value UI Undo was not one step");
            editor.redo();
            frame();
            check(editor.document()->source().document == adjusted, "Value UI Redo differs");
            const auto no_op_revision = editor.document()->revision();
            click("animation:reset_values");
            click("animation:transform_values");
            check(editor.document()->revision() == no_op_revision,
                  "Reset/identity changed source or added history");
            number("animation:value_scale_multiplier", 0, "10000");
            click("animation:transform_values");
            check(editor.document()->source().document == adjusted &&
                      editor.document()->revision() == no_op_revision,
                  "Rejected value edit changed source/history");
            click("animation:reset_values");
            auto escape = [&] {
                io.AddKeyEvent(ImGuiKey_Escape, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Escape, false);
                frame();
            };
            auto captured = [&](const char* phase) {
                check(
                    !test::ui_targets.at("animation:transform_values").enabled &&
                        !test::ui_targets.at("animation:reset_values").enabled &&
                        editor.document()->source().document == adjusted,
                    std::string("Captured gesture permits value Apply or changed source: ") +
                        phase + " apply=" +
                        std::to_string(test::ui_targets.at("animation:transform_values").enabled) +
                        " reset=" +
                        std::to_string(test::ui_targets.at("animation:reset_values").enabled) +
                        " same=" +
                        std::to_string(editor.document()->source().document == adjusted));
            };
            auto capture_ruler = target("animation:ruler");
            ImVec2 pointer{capture_ruler.minimum.x +
                               (capture_ruler.maximum.x - capture_ruler.minimum.x) * .5f,
                           capture_ruler.minimum.y + 50};
            mouse(pointer, false);
            mouse(pointer, true);
            captured("key-down");
            mouse({pointer.x + 20, pointer.y}, true);
            captured("key");
            escape();
            mouse(pointer, false);
            io.AddKeyEvent(ImGuiMod_Shift, true);
            frame();
            mouse(pointer, true);
            captured("box");
            escape();
            io.AddKeyEvent(ImGuiMod_Shift, false);
            mouse(pointer, false);
            io.AddMousePosEvent(pointer.x, pointer.y);
            io.AddMouseButtonEvent(2, true);
            frame();
            captured("pan");
            escape();
            io.AddMouseButtonEvent(2, false);
            frame();
            const auto ruler = target("animation:ruler");
            const ImVec2 p{ruler.minimum.x + (ruler.maximum.x - ruler.minimum.x) * .25f,
                           ruler.minimum.y + 50};
            mouse(p, false);
            mouse(p, true);
            mouse(p, false);
            const double seconds = std::clamp(double(std::floor(p.x) - ruler.minimum.x) /
                                                  (ruler.maximum.x - ruler.minimum.x),
                                              0., 1.) *
                                   2;
            const auto preview = editor.preview(scene.document());
            check(editor.preview_active() && scene.document() == authored_scene,
                  "Value preview changed authored Scene");
            for (const auto& row : preview["entities"])
                if (row["id"] == actor) {
                    const auto& c = row["components"];
                    check(std::abs(c["forge.local_translation"]["x"].get<double>() -
                                   (1 - 4 * seconds)) < .001 &&
                              std::abs(c["forge.local_scale"]["x"].get<double>() -
                                       (1.5 + 1.5 * seconds)) < .001,
                          "Adjusted independent preview position/scale differs");
                }
            locked = true;
            frame();
            check(!test::ui_targets.at("animation:transform_values").enabled &&
                      !test::ui_targets.at("animation:reset_values").enabled,
                  "Readonly value controls enabled");
            locked = false;
            frame();
            editor.request_save();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            do {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while ((editor.pending() || editor.dirty()) &&
                     std::chrono::steady_clock::now() < deadline);
            check(!editor.pending() && !editor.dirty(), "Value Save/Publish stalled");
            TransformAnimationDocument reopened(project.writer_guard(),
                                                "Assets/Values.animation.json");
            check(reopened.source().document == adjusted && scene.document() == authored_scene,
                  "Value save/reopen changed source or authored Scene");
            check(AssetCatalog::open_project(root).records().contains(source.asset()),
                  "Value clip not published");
            ImGui::DestroyContext();
        }
        std::cout << "Actual value "
                     "fields/selection/history/reset/rejection/preview/readonly/publication100/150/"
                     "200% passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
