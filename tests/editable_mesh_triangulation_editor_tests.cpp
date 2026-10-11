#include "editor/editable_mesh_editor.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need triangulation UI scratch root");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            const auto source = EditableMeshSource::create_cube(AssetId::generate());
            atomic_write(root / "Assets/Cube.mesh.json", source.document.dump(2));
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
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ui::interface_scale = scale;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            EditableMeshEditor editor;
            editor.open(project, "Assets/Cube.mesh.json");
            std::string message;
            bool locked = false;
            auto frame = [&] {
                test::ui_targets.clear();
                editor.poll(project, message);
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1600, 900}, ImGuiCond_Always);
                editor.draw(project, locked);
                ImGui::Render();
            };
            auto mouse = [&](ImVec2 at, bool down) {
                io.AddMousePosEvent(at.x, at.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto target = [&](const std::string& name) {
                frame();
                require(test::ui_targets.contains(name), "Missing triangulation target");
                auto box = test::ui_targets.at(name);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* w = ImGui::FindWindowByName("###Editable Mesh");
                    require(w, "Mesh window missing");
                    ImGui::SetScrollY(w, w->Scroll.y + (box.minimum.y + box.maximum.y -
                                                        box.clip_minimum.y - box.clip_maximum.y) /
                                                           2);
                    frame();
                    frame();
                    box = test::ui_targets.at(name);
                }
                require(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                            box.maximum.y <= box.clip_maximum.y,
                        "Disabled/clipped brush target");
                return box;
            };
            auto click = [&](const std::string& name) {
                const auto box = target(name);
                const ImVec2 at{(box.minimum.x + box.maximum.x) / 2,
                                (box.minimum.y + box.maximum.y) / 2};
                mouse(at, false);
                mouse(at, true);
                mouse(at, false);
                frame();
            };
            frame();
            frame();
            const auto baseline = editor.document()->source().document;
            click("button:Triangulate face");
            const auto triangulated = editor.document()->source().document;
            auto oracle = source;
            oracle.triangulate_faces(std::array<std::uint32_t, 1>{13});
            require(triangulated == oracle.document && editor.document()->can_undo(),
                    "Actual face action differs from source oracle/history");
            click("button:Undo");
            auto reserved = baseline;
            reserved["next_id"] = triangulated.at("next_id");
            require(editor.document()->source().document == reserved,
                    "Triangulation Undo lost geometry or allocator high-water");
            click("button:Redo");
            require(editor.document()->source().document == triangulated,
                    "Triangulation Redo differs");
            click("button:Undo");
            locked = true;
            frame();
            require(test::ui_targets.contains("button:Triangulate Mesh") &&
                        !test::ui_targets.at("button:Triangulate Mesh").enabled,
                    "Locked topology action enabled");
            locked = false;
            frame();
            click("button:Triangulate Mesh");
            const auto all = editor.document()->source().document;
            oracle = {reserved};
            oracle.triangulate_faces(std::array<std::uint32_t, 6>{9, 10, 11, 12, 13, 14});
            require(all == oracle.document && !editor.document()->can_redo(),
                    "Whole-Mesh branch differs from source oracle");
            for (const auto& face : all.at("faces"))
                require(face.at("id") != 15, "Branch reused discarded triangle ID");
            frame();
            require(!test::ui_targets.at("button:Triangulate Mesh").enabled,
                    "Triangle-only Mesh action remains enabled");
            require(all.at("vertices") == baseline.at("vertices"),
                    "Topology action moved vertices/colors");
            click("button:Save & Publish");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                frame();
            }
            require(!editor.pending() && !editor.dirty(), "Triangle publication failed");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Published triangle document did not close");
            editor.open(project, "Assets/Cube.mesh.json");
            frame();
            require(editor.document()->source().document == all,
                    "Triangle save/reopen lost values");
            require(scene.document() == scene_before,
                    "Topology action changed Scene ownership/history");
            test::observe_ui = false;
            ImGui::DestroyContext();
            std::filesystem::remove_all(root);
        }
        ui::interface_scale = 1;
        std::cout << "Actual triangulation "
                     "face/Mesh/lock/branch/Undo/Redo/publish/reopen65/100/200% passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
