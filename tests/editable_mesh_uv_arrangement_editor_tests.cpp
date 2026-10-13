#include "editor/editable_mesh_editor.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Mesh input was accepted");
}
} // namespace
#include "editable_mesh_uv_tests.hpp"
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need UV arrangement UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = uv_strip();
            for (auto& face : source.document["faces"])
                for (auto& corner : face["corners"]) {
                    const auto u = corner["uv"][0].get<double>();
                    corner["uv"][0] = u == 0 ? .1 : u == 1 ? .25 : .9;
                    corner["uv"][1] = .1 + corner["uv"][1].get<double>() * .8;
                }
            const auto original = source.document;
            atomic_write(root / "Assets/Arrangement.mesh.json", original.dump(2));
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto authored = scene.document();
            EditableMeshEditor editor;
            editor.open(project, "Assets/Arrangement.mesh.json");
            require(editor.document(), "Packing Mesh did not open");
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2500, 2800};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::style(scale);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            std::string message;
            bool locked = false;
            float window_width = 2400;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({window_width, 2650}, ImGuiCond_Always);
                editor.draw(project, locked);
                ImGui::Render();
            };
            auto target = [&](const std::string& id) {
                require(test::ui_targets.contains(id), "Missing arrangement control " + id);
                auto box = test::ui_targets.at(id);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Editable Mesh");
                    require(window, "Mesh window missing");
                    ImGui::SetScrollY(window,
                                      window->Scroll.y + (box.minimum.y + box.maximum.y -
                                                          box.clip_minimum.y - box.clip_maximum.y) /
                                                             2);
                    frame();
                    frame();
                    box = test::ui_targets.at(id);
                }
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                require(box.enabled && p.x >= box.clip_minimum.x && p.x <= box.clip_maximum.x &&
                            p.y >= box.clip_minimum.y && p.y <= box.clip_maximum.y,
                        "Clipped/disabled arrangement control " + id);
                return p;
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
            auto choose = [&](const std::string& id, int index) {
                click(id);
                io.AddKeyEvent(ImGuiKey_Home, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Home, false);
                frame();
                for (int i = 0; i < index; ++i) {
                    io.AddKeyEvent(ImGuiKey_DownArrow, true);
                    frame();
                    io.AddKeyEvent(ImGuiKey_DownArrow, false);
                    frame();
                }
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            auto key = [&](ImGuiKey modifier, bool down) {
                io.AddKeyEvent(modifier, down);
                frame();
            };
            auto uv_point = [&](double u, double v) {
                target("mesh:uv_canvas");
                const auto box = test::ui_targets.at("mesh:uv_canvas");
                const auto base = std::max(
                    16.f, std::min(box.maximum.x - box.minimum.x, box.maximum.y - box.minimum.y) -
                              36 * scale);
                return ImVec2{(box.minimum.x + box.maximum.x) / 2 + float(u - .5) * base,
                              (box.minimum.y + box.maximum.y) / 2 + float(v - .5) * base};
            };
            auto box_select = [&](ImVec2 a, ImVec2 b, bool held = true) {
                mouse(a, false);
                mouse(a, true);
                if (held)
                    mouse(b, true);
                mouse(b, false);
                frame();
            };
            frame();
            frame();
            click("mesh:uv_tab");
            click("button:Frame tile");
            const auto revision = editor.document()->revision();
            const auto initial_dirty =
                editor.dirty(); // New source still needs its first publication.
            const std::set<MeshUvCorner> all{{7, 0}, {7, 1}, {7, 2}, {7, 3},
                                             {8, 0}, {8, 1}, {8, 2}, {8, 3}};
            click("button:Select all UVs");
            require(editor.observed_uv_selection() == all, "Select all omitted UV corners");
            click("button:Invert UV selection");
            require(editor.observed_uv_selection().empty(), "Invert all did not clear UVs");
            click("button:Invert UV selection");
            require(editor.observed_uv_selection() == all, "Invert empty omitted UVs");
            click("button:Clear UV selection");
            click("mesh:uv_box_mode");
            auto left = uv_point(.1, .1), right = uv_point(.9, .1);
            const auto delta = 6 * scale;
            const auto lo = ImVec2{left.x - delta, left.y - delta},
                       hi = ImVec2{left.x + delta, left.y + delta};
            box_select(lo, hi, false);
            require(editor.observed_uv_selection() == std::set<MeshUvCorner>{{7, 0}},
                    "Release-only box selection omitted its final pointer");
            key(ImGuiMod_Ctrl, true);
            box_select({right.x - delta, right.y - delta}, {right.x + delta, right.y + delta});
            key(ImGuiMod_Ctrl, false);
            require(editor.observed_uv_selection() == std::set<MeshUvCorner>{{7, 0}, {8, 1}},
                    "Ctrl box did not add to original selection");
            key(ImGuiMod_Shift, true);
            box_select(hi, lo);
            key(ImGuiMod_Shift, false);
            require(editor.observed_uv_selection() == std::set<MeshUvCorner>{{8, 1}},
                    "Shift/reversed box did not subtract selected corners");
            const auto selected = editor.observed_uv_selection();
            mouse(lo, false);
            mouse(lo, true);
            mouse(hi, true);
            require(editor.observed_uv_selection() == selected,
                    "Held preview replaced UV selection early");
            key(ImGuiKey_Escape, true);
            key(ImGuiKey_Escape, false);
            mouse(hi, false);
            require(editor.observed_uv_selection() == selected,
                    "Escape committed a pending rectangle");
            mouse(lo, false);
            mouse(lo, true);
            mouse(hi, true);
            io.AddFocusEvent(false);
            frame();
            mouse(hi, false);
            io.AddFocusEvent(true);
            frame();
            frame();
            require(editor.observed_uv_selection() == selected,
                    "Focus loss accepted pending selection");
            target("mesh:uv_canvas");
            mouse(lo, false);
            mouse(lo, true);
            mouse(hi, true);
            editor.undo();
            mouse(hi, false);
            require(editor.observed_uv_selection() == selected,
                    "History request accepted pending selection");
            choose("mesh:uv_mode", 1);
            left = uv_point(.1, .1);
            box_select({left.x - delta, left.y - delta}, {left.x + delta, left.y + delta});
            require(editor.observed_uv_selection() ==
                        std::set<MeshUvCorner>{{7, 0}, {7, 1}, {7, 2}, {7, 3}},
                    "Face box did not expand touched corners to their full face");
            choose("mesh:uv_mode", 2);
            left = uv_point(.1, .1);
            box_select({left.x - delta, left.y - delta}, {left.x + delta, left.y + delta});
            require(editor.observed_uv_selection() == all,
                    "Island box did not cross connected UV edge");
            choose("mesh:uv_mode", 0);
            click("button:Clear UV selection");
            left = uv_point(.1, .1);
            const auto empty = editor.observed_uv_selection();
            mouse({left.x - delta, left.y - delta}, false);
            mouse({left.x - delta, left.y - delta}, true);
            mouse({left.x + delta, left.y + delta}, true);
            window_width -= 100;
            frame();
            mouse({left.x + delta, left.y + delta}, false);
            require(editor.observed_uv_selection() == empty, "Resize accepted a remapped box");
            window_width += 100;
            frame();
            left = uv_point(.1, .1);
            mouse({left.x - delta, left.y - delta}, false);
            mouse({left.x - delta, left.y - delta}, true);
            mouse({left.x + delta, left.y + delta}, true);
            locked = true;
            frame();
            mouse({left.x + delta, left.y + delta}, false);
            locked = false;
            frame();
            require(editor.observed_uv_selection() == empty,
                    "Play lock accepted pending box selection");
            require(editor.document()->source().document == original &&
                        editor.document()->revision() == revision &&
                        editor.dirty() == initial_dirty && !editor.document()->dirty() &&
                        !editor.can_undo(),
                    "Selection operations changed source/history/dirty state");
            click("mesh:uv_box_mode");
            click("button:Select all UVs");
            click("button:Distribute UV corners");
            auto expected = source;
            expected.distribute_uv(std::vector<MeshUvCorner>(all.begin(), all.end()), 0);
            const auto spaced = expected.document;
            require(editor.document()->source().document == spaced,
                    "UI spacing differs from source operation");
            click("button:Undo");
            require(editor.document()->source().document == original,
                    "Spacing Undo did not restore source");
            click("button:Redo");
            require(editor.document()->source().document == spaced, "Spacing Redo differs");
            choose("mesh:uv_arrange_axis", 1);
            choose("mesh:uv_alignment", 2);
            click("button:Align UV corners");
            expected.align_uv(std::vector<MeshUvCorner>(all.begin(), all.end()), 1,
                              MeshUvAlignment::Maximum);
            const auto aligned = expected.document;
            require(editor.document()->source().document == aligned,
                    "Axis/target alignment UI differs");
            click("button:Undo");
            require(editor.document()->source().document == spaced,
                    "Alignment was not one source Undo");
            click("button:Redo");
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty() && bool(editor.take_catalog()),
                    "Arranged UV publication failed");
            require(scene.document() == authored, "UV source edits changed the authored Scene");
            editor.request_close();
            frame();
            editor.open(project, "Assets/Arrangement.mesh.json");
            frame();
            require(editor.document()->source().document == aligned,
                    "Arranged UVs changed on reopen");
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual UV box/selection/arrangement UI at100/150/200% passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
