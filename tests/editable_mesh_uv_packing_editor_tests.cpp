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
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need UV packing UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = EditableMeshSource::create_cube(AssetId::generate());
            const auto original = source.document;
            atomic_write(root / "Assets/Packing.mesh.json", original.dump(2));
            std::vector<std::uint32_t> faces;
            for (const auto& row : original.at("faces"))
                faces.push_back(row.at("id").get<std::uint32_t>());
            auto expected = source;
            expected.pack_uv_islands(faces, double(.01f));
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto authored = scene.document();
            EditableMeshEditor editor;
            editor.open(project, "Assets/Packing.mesh.json");
            require(editor.document(), "Packing Mesh did not open");
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2500, 2800};
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
                require(test::ui_targets.contains(id), "Missing packing control " + id);
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
                        "Clipped/disabled packing control " + id);
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
            auto padding = [&](const char* value) {
                frame();
                target("mesh:uv_pack_padding");
                const auto box = test::ui_targets.at("mesh:uv_pack_padding");
                const ImVec2 p{box.minimum.x + 30 * scale, (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
                mouse(p, true);
                mouse(p, false);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
                io.AddInputCharactersUTF8(value);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            frame();
            frame();
            click("mesh:uv_tab");
            click("button:Select face");
            click("button:Frame tile");
            const auto revision = editor.document()->revision();
            click("button:Preview all UV islands");
            require(editor.document()->revision() == revision &&
                        editor.document()->source().document == original,
                    "Packing preview changed source/history");
            target("mesh:uv_canvas");
            const auto canvas = test::ui_targets.at("mesh:uv_canvas");
            const float base = std::max(16.f, std::min(canvas.maximum.x - canvas.minimum.x,
                                                       canvas.maximum.y - canvas.minimum.y) -
                                                  36 * scale);
            const auto& active = expected.document.at("faces")[4].at("corners")[0].at("uv");
            const ImVec2 packed_point{(canvas.minimum.x + canvas.maximum.x) / 2 +
                                          float(active[0].get<double>() - .5) * base,
                                      (canvas.minimum.y + canvas.maximum.y) / 2 +
                                          float(active[1].get<double>() - .5) * base};
            bool drawn = false;
            for (const auto* list : ImGui::GetDrawData()->CmdLists)
                for (const auto& vertex : list->VtxBuffer)
                    drawn |= vertex.col == IM_COL32(255, 226, 136, 255) &&
                             std::abs(vertex.pos.x - packed_point.x) < 9 * scale &&
                             std::abs(vertex.pos.y - packed_point.y) < 9 * scale;
            require(drawn, "Packing preview did not draw the applied candidate corner");
            click("button:Cancel UV packing");
            require(editor.document()->source().document == original &&
                        editor.document()->revision() == revision,
                    "Cancel packing edited source");
            click("button:Preview all UV islands");
            click("mesh:uv_pack_rotate");
            require(editor.document()->source().document == original,
                    "Packing option edited source before Apply");
            click("mesh:uv_pack_rotate");
            padding("0.25");
            require(!test::ui_targets.at("button:Apply UV packing").enabled &&
                        editor.document()->source().document == original &&
                        editor.document()->revision() == revision,
                    "Impossible UI padding remained applicable or replaced source");
            padding("0.01");
            require(test::ui_targets.at("button:Apply UV packing").enabled,
                    "Corrected packing padding did not recover preview");
            click("button:Cancel UV packing");
            click("button:Preview selected UV islands");
            click("button:Apply UV packing");
            auto selected = source;
            selected.pack_uv_islands(std::array<std::uint32_t, 1>{13}, double(.01f));
            require(editor.document()->source().document == selected.document,
                    "Selected packing changed unrelated islands");
            click("button:Undo");
            require(editor.document()->source().document == original,
                    "Packing was not one Undo step");
            click("button:Redo");
            require(editor.document()->source().document == selected.document,
                    "Packing Redo differs");
            click("button:Undo");
            click("button:Preview all UV islands");
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            frame();
            require(!test::ui_targets.contains("button:Apply UV packing") &&
                        editor.document()->source().document == original,
                    "Escape retained packing intent");
            click("button:Preview all UV islands");
            locked = true;
            frame();
            require(!test::ui_targets.contains("button:Apply UV packing") &&
                        !test::ui_targets.at("button:Preview all UV islands").enabled &&
                        editor.document()->source().document == original,
                    "Read-only state retained packing edit");
            locked = false;
            frame();
            click("button:Preview all UV islands");
            click("button:Apply UV packing");
            require(editor.document()->source().document == expected.document,
                    "All-island packing differs from source operation");
            const auto applied = editor.document()->source().document;
            click("button:Preview all UV islands");
            click("button:Undo");
            require(editor.document()->source().document == original &&
                        !test::ui_targets.contains("button:Apply UV packing"),
                    "Undo did not cancel packing preview");
            click("button:Redo");
            require(editor.document()->source().document == applied, "All-island Redo differs");
            click("button:Frame tile");
            target("mesh:uv_canvas");
            const auto resized_canvas = test::ui_targets.at("mesh:uv_canvas");
            const auto resized_base =
                std::max(16.f, std::min(resized_canvas.maximum.x - resized_canvas.minimum.x,
                                        resized_canvas.maximum.y - resized_canvas.minimum.y) -
                                   36 * scale);
            const auto& corner = applied.at("faces")[4].at("corners")[0].at("uv");
            const ImVec2 drag_start{(resized_canvas.minimum.x + resized_canvas.maximum.x) / 2 +
                                        float(corner[0].get<double>() - .5) * resized_base,
                                    (resized_canvas.minimum.y + resized_canvas.maximum.y) / 2 +
                                        float(corner[1].get<double>() - .5) * resized_base};
            mouse(drag_start, false);
            mouse(drag_start, true);
            mouse({drag_start.x + 20, drag_start.y + 10}, true);
            window_width -= 100;
            frame();
            mouse({drag_start.x + 20, drag_start.y + 10}, false);
            frame();
            require(editor.document()->source().document == applied,
                    "Canvas resize committed a remapped UV drag");
            window_width += 100;
            frame();
            click("button:Preview all UV islands");
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty() && bool(editor.take_catalog()) &&
                        editor.document()->source().document == applied &&
                        !test::ui_targets.contains("button:Apply UV packing"),
                    "Save retained unaccepted packing preview or failed publication");
            require(scene.document() == authored, "UV packing changed Scene source");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Clean packing document failed to close");
            editor.open(project, "Assets/Packing.mesh.json");
            frame();
            require(editor.document()->source().document == applied, "Packing save/reopen differs");
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual packing UI100/150/200% "
                     "preview/cancel/scope/Apply/Undo/lock/Save/reopen passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
