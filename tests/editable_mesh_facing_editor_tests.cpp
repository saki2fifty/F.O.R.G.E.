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
        require(argc == 2, "Need facing UI scratch root");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = EditableMeshSource::create_cube(AssetId::generate());
            for (auto& face : source.document.at("faces"))
                for (auto& corner : face.at("corners"))
                    corner["extension"] = {{"retained", corner.at("vertex")}};
            atomic_write(root / "Assets/Facing.mesh.json", source.document.dump(2));
            { // Release project writer leases before removing scratch files on Windows.
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
                editor.open(project, "Assets/Facing.mesh.json");
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
                    if (!test::ui_targets.contains(name)) {
                        std::cerr << "Missing facing target: " << name << '\n';
                        require(false, "Missing facing target");
                    }
                    auto box = test::ui_targets.at(name);
                    if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                        auto* w = ImGui::FindWindowByName("###Editable Mesh");
                        require(w, "Mesh window missing");
                        ImGui::SetScrollY(w,
                                          w->Scroll.y + (box.minimum.y + box.maximum.y -
                                                         box.clip_minimum.y - box.clip_maximum.y) /
                                                            2);
                        frame();
                        frame();
                        box = test::ui_targets.at(name);
                    }
                    require(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                                box.maximum.y <= box.clip_maximum.y,
                            "Disabled/clipped facing target");
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
                auto oracle = source;
                std::vector<std::uint32_t> faces;
                for (const auto& face : source.document.at("faces"))
                    faces.push_back(face.at("id").get<std::uint32_t>());
                oracle.reverse_faces(faces);
                click("mesh:uv_tab");
                click("mesh:uv_corner:13:1");
                click("mesh:model_tab");
                click("button:Reverse connected surface");
                const auto reversed = editor.document()->source().document;
                require(reversed == oracle.document && editor.document()->can_undo(),
                        "Connected facing action differs from atomic source");
                // Reversed corner indices must not retain the old one-corner UV scope.
                click("mesh:uv_tab");
                click("button:Mirror U");
                auto uv_oracle = oracle;
                uv_oracle.transform_uv(std::array{MeshUvCorner{13, 0}, MeshUvCorner{13, 1},
                                                  MeshUvCorner{13, 2}, MeshUvCorner{13, 3}},
                                       {}, 0, {-1, 1});
                require(editor.document()->source().document == uv_oracle.document,
                        "Facing reversal retained stale UV corner selection or cache");
                click("button:Undo");
                require(editor.document()->source().document == reversed,
                        "UV edit after facing did not undo exactly");
                click("mesh:model_tab");
                click("button:Undo");
                require(editor.document()->source().document == baseline, "Facing Undo differs");
                click("button:Redo");
                require(editor.document()->source().document == reversed, "Facing Redo differs");
                locked = true;
                frame();
                require(!test::ui_targets.at("button:Reverse connected surface").enabled &&
                            !test::ui_targets.at("button:Reverse Mesh facing").enabled,
                        "Play lock permits facing edits");
                locked = false;
                frame();
                click("button:Reverse Mesh facing");
                require(editor.document()->source().document == baseline,
                        "Whole facing action did not restore exact corner metadata/allocator");
                click("button:Undo");
                click("button:Undo");
                require(editor.document()->source().document == baseline,
                        "Facing history is not one step per action");
                click("button:Reverse Mesh facing");
                require(editor.document()->source().document == reversed &&
                            !editor.document()->can_redo(),
                        "Facing branch changed source or retained discarded redo");
                click("button:Save & Publish");
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    frame();
                }
                require(!editor.pending() && !editor.dirty(), "Facing publication failed");
                editor.request_close();
                frame();
                require(!editor.is_open(), "Published facing document did not close");
                editor.open(project, "Assets/Facing.mesh.json");
                frame();
                require(editor.document()->source().document == reversed,
                        "Facing save/reopen lost full data or identities");
                require(scene.document() == scene_before, "Facing changed Scene ownership/history");
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
            std::filesystem::remove_all(root);
        }
        ui::interface_scale = 1;
        std::cout << "Actual connected/all facing, lock, history/branch, publication and "
                     "reopen65/100/200% passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
