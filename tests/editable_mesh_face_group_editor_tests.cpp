#include "editor/editable_mesh_editor.hpp"
#include "model_render_resource.hpp"
#include "runtime_package.hpp"
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
        require(argc == 2, "Need Model face group UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = EditableMeshSource::create_cube(AssetId::generate());
            const auto original = source.document;
            atomic_write(root / "Assets/Faces.mesh.json", original.dump(2));
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto authored = scene.document();
            EditableMeshEditor editor;
            editor.open(project, "Assets/Faces.mesh.json");
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
                require(test::ui_targets.contains(id), "Missing face group control " + id);
                auto box = test::ui_targets.at(id);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Editable Mesh");
                    require(window, "Mesh window missing");
                    if (id.starts_with("mesh:face:"))
                        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
                            if (candidate->ParentWindow == window &&
                                std::string_view(candidate->Name).find("/Mesh elements_") !=
                                    std::string_view::npos) {
                                window = candidate;
                                break;
                            }
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
                        "Clipped/disabled face group control " + id);
                return p;
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto click = [&](const std::string& id) {
                frame();
                if (id.starts_with("mesh:face:"))
                    target("mesh:vertex_list");
                const auto p = target(id);
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto key = [&](ImGuiKey code) {
                io.AddKeyEvent(code, true);
                frame();
                io.AddKeyEvent(code, false);
                frame();
            };
            auto number = [&](const std::string& id, const char* value) {
                target(id);
                const auto box = test::ui_targets.at(id);
                const ImVec2 p{box.minimum.x + 20 * scale, (box.minimum.y + box.maximum.y) / 2};
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(value);
                frame();
                key(ImGuiKey_Enter);
            };
            auto face_set = [&](const Json& doc) {
                std::set<std::uint32_t> faces;
                for (const auto& face : doc.at("faces"))
                    faces.insert(face.at("id").get<std::uint32_t>());
                return faces;
            };
            frame();
            frame();
            click("mesh:model_tab");
            const auto revision = editor.document()->revision();
            const auto initial_dirty = editor.dirty();
            click("mesh:face:9");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            click("mesh:face:13");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            const std::set<std::uint32_t> pair{9, 13};
            require(editor.observed_face_selection() == pair,
                    "Ctrl face list did not add a second face");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            click("mesh:face:13");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            require(editor.observed_face_selection() == std::set<std::uint32_t>{9},
                    "Ctrl face list did not toggle off");
            click("button:Select connected faces");
            require(editor.observed_face_selection() == face_set(original),
                    "Connected cube selection omitted shared-edge faces");
            click("button:Invert face selection");
            require(editor.observed_face_selection().empty(), "Invert all did not clear faces");
            require(!test::ui_targets.at("button:Apply transform").enabled &&
                        !test::ui_targets.at("button:Inset selected face").enabled &&
                        !test::ui_targets.at("button:Extrude selected face").enabled,
                    "Empty Model selection retained active edit controls");
            click("button:Select all faces");
            click("button:Clear face selection");
            target("mesh:model_canvas");
            const auto canvas = test::ui_targets.at("mesh:model_canvas");
            const ImVec2 center{(canvas.minimum.x + canvas.maximum.x) / 2,
                                (canvas.minimum.y + canvas.maximum.y) / 2};
            mouse(center, false);
            mouse(center, true);
            mouse(center, false);
            frame();
            require(editor.observed_face_selection().size() == 1,
                    "Canvas did not choose one nearest face");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            mouse(center, false);
            mouse(center, true);
            mouse(center, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            require(editor.observed_face_selection().empty(),
                    "Canvas toggled multiple overlapping depth hits");
            click("mesh:face:9");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            click("mesh:face:13");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            require(editor.document()->source().document == original &&
                        editor.document()->revision() == revision &&
                        editor.dirty() == initial_dirty && !editor.document()->dirty() &&
                        !editor.can_undo(),
                    "Face selection edited source/history/dirty state");
            click("button:Reverse selected faces");
            require(editor.document()->source().document == original &&
                        editor.document()->revision() == revision,
                    "Partial winding reversal was not rejected atomically");
            click("button:Triangulate selected faces");
            auto expected = source;
            expected.triangulate_faces(std::array<std::uint32_t, 2>{9, 13});
            const auto split = expected.document;
            require(editor.document()->source().document == split,
                    "Selected triangulation differs from source kernel");
            const std::set<std::uint32_t> generated{9, 13, 15, 16};
            require(editor.observed_face_selection() == generated,
                    "Generated triangles were not selected");
            click("button:Undo");
            require(editor.document()->source().document.at("faces") == original.at("faces") &&
                        editor.observed_face_selection() == pair,
                    "Topology Undo failed to prune generated selection IDs");
            click("button:Redo");
            require(editor.document()->source().document == split &&
                        editor.observed_face_selection() == pair,
                    "Redo incorrectly replayed a transient face selection");
            click("button:Triangulate Mesh");
            const auto triangles = editor.document()->source().document;
            click("mesh:face:9");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            click("mesh:face:13");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            std::set<std::uint32_t> vertices;
            std::size_t incidences = 0;
            for (const auto& face : triangles.at("faces"))
                if (pair.contains(face.at("id").get<std::uint32_t>()))
                    for (const auto& corner : face.at("corners")) {
                        vertices.insert(corner.at("vertex").get<std::uint32_t>());
                        ++incidences;
                    }
            require(vertices.size() < incidences, "Group transform fixture has no shared vertices");
            number("mesh:model_move", "0.125");
            click("button:Apply transform");
            const auto moved = editor.document()->source().document;
            require(moved.at("faces") == triangles.at("faces"), "Face transform changed topology");
            for (std::size_t i = 0; i < triangles.at("vertices").size(); ++i) {
                auto vertex = triangles.at("vertices")[i];
                if (vertices.contains(vertex.at("id").get<std::uint32_t>()))
                    vertex["position"][0] = vertex.at("position")[0].get<double>() + .125;
                require(moved.at("vertices")[i] == vertex,
                        "Shared vertex moved twice or an unselected vertex moved");
            }
            click("button:Undo");
            require(editor.document()->source().document == triangles,
                    "Group transform was not one source Undo");
            click("button:Redo");
            number("mesh:model_scale", "0");
            click("button:Apply transform");
            require(editor.document()->source().document == moved,
                    "Invalid face group scale replaced good source");
            click("button:Cancel transform");
            click("button:Select all faces");
            click("mesh:preview_transform");
            require(!test::ui_targets.at("button:Reverse selected faces").enabled &&
                        !test::ui_targets.at("button:Triangulate selected faces").enabled,
                    "Derived transform preview retained competing topology edits");
            click("button:Cancel transform");
            locked = true;
            frame();
            require(!test::ui_targets.at("button:Reverse selected faces").enabled &&
                        !test::ui_targets.at("button:Apply transform").enabled,
                    "Play lock retained face group edits");
            locked = false;
            frame();
            click("button:Reverse selected faces");
            expected = {moved};
            const auto all_faces = face_set(moved);
            expected.reverse_faces(std::vector<std::uint32_t>(all_faces.begin(), all_faces.end()));
            const auto reversed = expected.document;
            require(editor.document()->source().document == reversed,
                    "Selected surface reversal differs");
            click("button:Undo");
            require(editor.document()->source().document == moved,
                    "Selected reversal was not one source Undo");
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
                    "Face group publication failed");
            const auto target_profile = desktop_texture_target();
            const auto mesh_id = editor.document()->source().asset();
            const auto expected_mesh = editor.document()->source().cook();
            const auto output = root.parent_path() / ("face-export-" + AssetId::generate().str());
            const std::array roots{mesh_id};
            package_runtime_content(root, output, roots,
                                    {target_profile.platform, target_profile.backend});
            const auto relocated =
                root.parent_path() / ("face-relocated-" + AssetId::generate().str());
            std::filesystem::rename(output, relocated);
            const auto offline = root / "Assets/Faces.mesh.offline";
            std::filesystem::rename(root / "Assets/Faces.mesh.json", offline);
            require(!std::filesystem::exists(relocated / "Assets/Faces.mesh.json"),
                    "Model source leaked into export");
            const auto shipped = std::make_shared<const AssetCatalog>(
                open_runtime_content(relocated, {target_profile.platform, target_profile.backend}));
            ResourcePool<MeshAsset> pool;
            const auto ticket =
                asset_detail::request_model_mesh(pool, relocated, shipped, {mesh_id});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Relocated face group Mesh cannot load");
            const auto resource = pool.acquire(ticket);
            require(encode_mesh(resource->mesh) == encode_mesh(expected_mesh),
                    "Face group resource/export differs from authored cook");
            std::filesystem::rename(offline, root / "Assets/Faces.mesh.json");
            require(scene.document() == authored, "Face group edits changed Scene source");
            editor.request_close();
            frame();
            editor.open(project, "Assets/Faces.mesh.json");
            frame();
            require(editor.document()->source().document == reversed &&
                        editor.observed_face_selection().size() == 1,
                    "Reopen changed source or persisted transient face groups");
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual Model face groups100/150/200% "
                     "selection/transform/topology/history/publication passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
