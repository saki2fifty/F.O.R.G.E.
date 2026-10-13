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
        require(argc == 2, "Need Model topology cleanup UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = EditableMeshSource::create_cube(AssetId::generate());
            const auto original = source.document;
            atomic_write(root / "Assets/Topology.mesh.json", original.dump(2));
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto authored = scene.document();
            EditableMeshEditor editor;
            editor.open(project, "Assets/Topology.mesh.json");
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
                require(test::ui_targets.contains(id), "Missing topology cleanup control " + id);
                auto box = test::ui_targets.at(id);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* window = ImGui::FindWindowByName("###Editable Mesh");
                    require(window, "Mesh window missing");
                    if (id.starts_with("mesh:face:") || id.starts_with("mesh:edge:"))
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
                        "Clipped/disabled topology cleanup control " + id);
                return p;
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto click = [&](const std::string& id) {
                frame();
                if (id.starts_with("mesh:face:") || id.starts_with("mesh:edge:"))
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
            auto edge_mode = [&] {
                click("mesh:selection_mode");
                key(ImGuiKey_Home);
                key(ImGuiKey_DownArrow);
                key(ImGuiKey_Enter);
                frame();
            };
            auto disabled = [&](const std::string& id) {
                frame();
                require(test::ui_targets.contains(id) && !test::ui_targets.at(id).enabled,
                        "Expected disabled topology control " + id);
            };
            auto save = [&] {
                editor.request_save();
                frame();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                    frame();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                frame();
                require(!editor.dirty() && bool(editor.take_catalog()),
                        "Topology source publication failed");
            };
            frame();
            frame();
            click("mesh:model_tab");
            const auto revision = editor.document()->revision();
            click("button:Remove unused vertices");
            require(editor.document()->revision() == revision && !editor.document()->can_undo(),
                    "No-op cleanup created source history");
            click("button:Clear face selection");
            disabled("button:Delete selected faces");
            click("button:Select all faces");
            disabled("button:Delete selected faces");
            click("mesh:face:9");
            click("button:Delete selected faces");
            auto deleted = original;
            deleted["faces"].erase(deleted["faces"].begin());
            require(editor.document()->source().document == deleted &&
                        !editor.observed_face_selection().contains(9) &&
                        editor.observed_face_selection().size() == 1,
                    "Face deletion or surviving selection differs");
            click("button:Undo");
            require(editor.document()->source().document == original,
                    "Face deletion was not one Undo step");
            click("button:Redo");
            require(editor.document()->source().document == deleted, "Face deletion Redo differs");
            click("button:Undo");
            click("button:Select all faces");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            click("mesh:face:13");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            click("button:Delete selected faces");
            auto expected = EditableMeshSource{original};
            expected.delete_faces(std::array<std::uint32_t, 5>{9, 10, 11, 12, 14});
            const auto one_face = expected.document;
            require(editor.document()->source().document == one_face &&
                        editor.observed_face_selection() == std::set<std::uint32_t>{13},
                    "Group deletion changed retained geometry or selection");
            click("button:Remove unused vertices");
            expected.remove_unused_vertices();
            const auto cleaned = expected.document;
            require(editor.document()->source().document == cleaned &&
                        cleaned.at("vertices").size() == 4,
                    "Explicit cleanup did not remove only unused vertices");
            click("button:Undo");
            require(editor.document()->source().document == one_face,
                    "Cleanup was not one Undo step");
            click("button:Redo");
            require(editor.document()->source().document == cleaned, "Cleanup Redo differs");
            const auto clean_revision = editor.document()->revision();
            click("button:Remove unused vertices");
            require(editor.document()->revision() == clean_revision,
                    "Repeated cleanup created source history");
            click("button:Undo");
            click("button:Undo");
            require(editor.document()->source().document == original,
                    "Cleanup/deletion history changed surviving source");
            click("mesh:face:9");
            click("button:Triangulate face");
            const auto triangles = editor.document()->source().document;
            edge_mode();
            click("mesh:edge:5:7");
            click("button:Join triangles");
            auto joined = original;
            joined["next_id"] = 16;
            require(editor.document()->source().document == joined &&
                        editor.observed_face_selection() == std::set<std::uint32_t>{9},
                    "Edge join failed to select the retained complete quad");
            disabled("button:Join triangles");
            click("button:Undo");
            require(editor.document()->source().document == triangles,
                    "Triangle join was not one Undo step");
            click("button:Redo");
            require(editor.document()->source().document == joined, "Triangle join Redo differs");
            edge_mode();
            click("mesh:edge:5:6");
            const auto safe_revision = editor.document()->revision();
            click("button:Join triangles");
            require(editor.document()->source().document == joined &&
                        editor.document()->revision() == safe_revision,
                    "Rejected nontriangle join changed source/history");
            locked = true;
            disabled("button:Join triangles");
            disabled("button:Remove unused vertices");
            locked = false;
            click("mesh:selection_mode");
            key(ImGuiKey_End);
            key(ImGuiKey_Enter);
            click("mesh:face:9");
            click("mesh:preview_transform");
            disabled("button:Delete selected faces");
            disabled("button:Remove unused vertices");
            disabled("button:Join triangles");
            key(ImGuiKey_Escape);
            click("button:Select all faces");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            click("mesh:face:9");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            click("button:Delete selected faces");
            click("button:Remove unused vertices");
            const auto final_source = editor.document()->source().document;
            require(final_source.at("faces").size() == 1 &&
                        final_source.at("vertices").size() == 4 && final_source.at("next_id") == 16,
                    "Joined/deleted/cleaned topology differs");
            save();
            const auto target_profile = desktop_texture_target();
            const auto mesh_id = editor.document()->source().asset();
            const auto expected_mesh = editor.document()->source().cook();
            const auto output =
                root.parent_path() / ("topology-export-" + AssetId::generate().str());
            const std::array roots{mesh_id};
            package_runtime_content(root, output, roots,
                                    {target_profile.platform, target_profile.backend});
            const auto relocated =
                root.parent_path() / ("topology-relocated-" + AssetId::generate().str());
            std::filesystem::rename(output, relocated);
            const auto offline = root / "Assets/Topology.mesh.offline";
            std::filesystem::rename(root / "Assets/Topology.mesh.json", offline);
            require(!std::filesystem::exists(relocated / "Assets/Topology.mesh.json"),
                    "Topology source leaked into export");
            const auto shipped = std::make_shared<const AssetCatalog>(
                open_runtime_content(relocated, {target_profile.platform, target_profile.backend}));
            ResourcePool<MeshAsset> pool;
            const auto ticket =
                asset_detail::request_model_mesh(pool, relocated, shipped, {mesh_id});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Relocated topology Mesh cannot load");
            const auto resource = pool.acquire(ticket);
            require(encode_mesh(resource->mesh) == encode_mesh(expected_mesh),
                    "Topology resource/export differs from authored cook");
            std::filesystem::rename(offline, root / "Assets/Topology.mesh.json");
            require(scene.document() == authored, "Topology edits changed Scene source");
            editor.request_close();
            frame();
            editor.open(project, "Assets/Topology.mesh.json");
            frame();
            require(editor.document()->source().document == final_source,
                    "Topology source save/reopen differs");
            editor.request_close();
            frame();
            auto seam = EditableMeshSource{triangles};
            seam.document["asset_id"] = AssetId::generate();
            seam.document["faces"][1]["corners"][0]["uv"][0] = .1;
            atomic_write(root / "Assets/Seam.mesh.json", seam.document.dump(2));
            editor.open(project, "Assets/Seam.mesh.json");
            frame();
            click("mesh:model_tab");
            edge_mode();
            click("mesh:edge:5:7");
            const auto seam_revision = editor.document()->revision();
            click("button:Join triangles");
            require(editor.document()->source().document == seam.document &&
                        editor.document()->revision() == seam_revision &&
                        !editor.document()->can_undo() &&
                        test::ui_targets.at("button:Join triangles").enabled,
                    "Rejected UV seam join changed source/history/edge selection");
            save();
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual Model topology cleanup100/150/200% "
                     "selection/history/publication/source-offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
