#include "editor/editable_mesh_editor.hpp"
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need UI scratch directory");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            const auto source = EditableMeshSource::create_cube(AssetId::generate());
            atomic_write(root / "Assets/Panel.mesh.json", source.document.dump(2));
            struct Cleanup {
                std::filesystem::path path;
                ~Cleanup() {
                    std::error_code ignored;
                    std::filesystem::remove_all(path, ignored);
                }
            } cleanup{root};
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto scene_before = scene.document();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {1600, 900};
            io.DeltaTime = 1.f / 60;
            io.FontGlobalScale = scale;
            ui::interface_scale = scale;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            EditableMeshEditor editor;
            editor.open(project, "Assets/Panel.mesh.json");
            require(editor.document() != nullptr, "Mesh document failed to open");
            const auto before = editor.document()->source().document;
            std::string message;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1600, 900}, ImGuiCond_Always);
                editor.draw(project, false);
                ImGui::Render();
            };
            auto target = [&](const std::string& name) {
                const auto at = test::ui_targets.find(name);
                require(at != test::ui_targets.end() && at->second.enabled,
                        "Missing/disabled target " + name);
                return at->second;
            };
            // Scroll is a layout precondition; the operations themselves use
            // actual mouse/key events. A normal document viewport must reach
            // every tested control, including 200% with a scrollable tool area.
            auto reveal = [&](const std::string& name) {
                frame();
                auto box = target(name);
                const auto center = (box.minimum.y + box.maximum.y) / 2;
                if (center < box.clip_minimum.y + 2 || center > box.clip_maximum.y - 2) {
                    auto* window = ImGui::FindWindowByName("###Editable Mesh");
                    require(window != nullptr, "Editable Mesh window missing");
                    ImGui::SetScrollY(window, window->Scroll.y + center -
                                                  (box.clip_minimum.y + box.clip_maximum.y) / 2);
                    frame();
                    frame();
                    box = target(name);
                }
                return box;
            };
            auto mouse = [&](ImVec2 point, bool down) {
                io.AddMousePosEvent(point.x, point.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto click = [&](const std::string& name) {
                frame();
                const auto box = reveal(name);
                const ImVec2 point{(box.minimum.x + box.maximum.x) / 2,
                                   (box.minimum.y + box.maximum.y) / 2};
                require(point.y >= box.clip_minimum.y && point.y <= box.clip_maximum.y,
                        "Clipped target " + name);
                mouse(point, false);
                mouse(point, true);
                mouse(point, false);
                frame();
            };
            auto number = [&](const std::string& name, const char* text) {
                frame();
                const auto box = reveal(name);
                const ImVec2 point{box.minimum.x + 80, (box.minimum.y + box.maximum.y) / 2};
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
                mouse(point, false);
                mouse(point, true);
                mouse(point, false);
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
            auto geometry = [&] {
                const auto box = reveal("mesh:model_canvas");
                int vertices = 0;
                const auto* data = ImGui::GetDrawData();
                for (int i = 0; i < data->CmdListsCount; ++i)
                    for (const auto& vertex : data->CmdLists[i]->VtxBuffer)
                        if (vertex.pos.x > box.minimum.x + 1 && vertex.pos.x < box.maximum.x - 1 &&
                            vertex.pos.y > box.minimum.y + 1 && vertex.pos.y < box.maximum.y - 1)
                            ++vertices;
                return vertices;
            };
            frame();
            frame();
            const auto original_geometry = geometry();
            number("mesh:inset_distance", "0.1");
            click("mesh:preview_inset");
            require(geometry() > original_geometry, "Inset preview did not draw new topology");
            require(editor.document()->source().document == before && !editor.can_undo(),
                    "Preview authored source/history");
            click("button:Cancel inset");
            require(geometry() == original_geometry &&
                        editor.document()->source().document == before,
                    "Cancel retained inset preview/source");
            click("mesh:preview_inset");
            io.AddKeyEvent(ImGuiKey_Escape, true);
            frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            frame();
            require(geometry() == original_geometry &&
                        editor.document()->source().document == before,
                    "Escape retained preview/source");
            click("button:Inset selected face");
            const auto inset = editor.document()->source().document;
            require(inset.at("vertices").size() == 12 && inset.at("faces").size() == 10 &&
                        editor.can_undo(),
                    "Accepted inset missing");
            number("mesh:extrude_distance", "-0.2");
            click("mesh:preview_extrude");
            require(editor.document()->source().document == inset,
                    "Extrusion preview authored source");
            click("button:Extrude selected face");
            const auto recessed = editor.document()->source().document;
            require(recessed.at("vertices").size() == 16 && recessed.at("faces").size() == 14,
                    "Inset did not keep inner face selected for recess");
            const auto floor = recessed.at("next_id");
            click("button:Undo");
            require(editor.document()->source().document.at("faces") == inset.at("faces") &&
                        editor.document()->source().document.at("next_id") == floor,
                    "Undo did not restore inset/reserved IDs");
            click("button:Undo");
            require(editor.document()->source().document.at("vertices") == before.at("vertices"),
                    "Inset Undo did not restore cube");
            click("button:Redo");
            click("button:Redo");
            require(editor.document()->source().document == recessed,
                    "Redo did not restore recessed panel");
            const auto safe_revision = editor.document()->revision();
            number("mesh:inset_distance", "100");
            click("button:Inset selected face");
            require(editor.document()->revision() == safe_revision &&
                        editor.document()->source().document == recessed,
                    "Rejected inset changed source/history");
            require(scene.document() == scene_before, "Mesh tools authored the Scene");
            test::observe_ui = false;
            ImGui::DestroyContext();
        }
        ui::interface_scale = 1;
        std::cout << "Actual mesh inset/recess preview, Cancel/Escape, selected inner face, "
                     "Undo/Redo/IDs and failed edit at65/100/200% in1600x900 passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
