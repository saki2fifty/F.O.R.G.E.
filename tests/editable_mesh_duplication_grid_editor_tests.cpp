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
        require(argc == 2, "Need Model duplication/grid UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = EditableMeshSource::create_cube(AssetId::generate());
            const auto original = source.document;
            atomic_write(root / "Assets/Copies.mesh.json", original.dump(2));
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto authored = scene.document();
            EditableMeshEditor editor;
            editor.open(project, "Assets/Copies.mesh.json");
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
                require(test::ui_targets.contains(id), "Missing duplication/grid control " + id);
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
                        "Clipped/disabled duplication/grid control " + id);
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
            auto face_pair = [&] {
                click("mesh:face:9");
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                click("mesh:face:13");
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
            };
            auto disabled = [&](const std::string& id) {
                frame();
                require(test::ui_targets.contains(id) && !test::ui_targets.at(id).enabled,
                        "Expected disabled placement control " + id);
            };
            frame();
            frame();
            click("mesh:model_tab");
            click("button:Clear face selection");
            disabled("button:Duplicate selected faces");
            disabled("button:Snap selection to grid");
            face_pair();
            number("mesh:duplicate_offset", "1.1");
            click("button:Duplicate selected faces");
            auto expected = EditableMeshSource{original};
            const auto copies =
                expected.duplicate_faces(std::array<std::uint32_t, 2>{9, 13}, {double(1.1f), 0, 0});
            const auto duplicated = expected.document;
            require(editor.document()->source().document == duplicated &&
                        editor.observed_face_selection() ==
                            std::set<std::uint32_t>(copies.begin(), copies.end()),
                    "Actual detached duplication/offset/generated selection differs");
            click("button:Undo");
            auto reserved = original;
            reserved["next_id"] = 23;
            require(editor.document()->source().document == reserved,
                    "Duplication Undo lost original data or reserved IDs");
            click("button:Redo");
            require(editor.document()->source().document == duplicated, "Duplication Redo differs");
            // Undo prunes transient face IDs; explicitly reselect both copies.
            click("mesh:face:21");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            click("mesh:face:22");
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            click("button:Snap selection to grid");
            const std::array<std::uint32_t, 6> new_vertices{15, 16, 17, 18, 19, 20};
            expected.snap_vertices(new_vertices, .25);
            const auto snapped = expected.document;
            require(editor.document()->source().document == snapped,
                    "Face-group snapping changed unselected geometry or shared vertices");
            click("button:Undo");
            require(editor.document()->source().document == duplicated,
                    "Grid snapping was not one Undo step");
            click("button:Redo");
            require(editor.document()->source().document == snapped, "Grid Redo differs");
            const auto revision = editor.document()->revision();
            click("button:Snap selection to grid");
            require(editor.document()->revision() == revision, "Aligned selection created history");
            click("mesh:grid_x");
            click("mesh:grid_y");
            click("mesh:grid_z");
            disabled("button:Snap selection to grid");
            click("mesh:grid_x");
            number("mesh:grid_spacing", "4");
            click("button:Snap selection to grid");
            require(editor.document()->source().document == snapped &&
                        editor.document()->revision() == revision,
                    "Collapsed grid snap changed source/history");
            number("mesh:grid_spacing", "0");
            click("button:Snap selection to grid");
            require(editor.document()->revision() == revision,
                    "Invalid grid spacing changed history");
            number("mesh:grid_spacing", "1");
            click("button:Snap selection to grid");
            expected.snap_vertices(new_vertices, 1, {true, false, false});
            require(editor.document()->source().document == expected.document,
                    "Actual axis mask and half-tie snapping differ");
            click("button:Undo");
            click("button:Undo");
            click("button:Undo");
            require(editor.document()->source().document == reserved,
                    "Placement history did not restore original with high-water");
            face_pair();
            number("mesh:duplicate_offset", "2.1");
            click("button:Duplicate selected faces");
            expected = {reserved};
            const auto branch_copies =
                expected.duplicate_faces(std::array<std::uint32_t, 2>{9, 13}, {double(2.1f), 0, 0});
            require(editor.document()->source().document == expected.document &&
                        branch_copies == std::vector<std::uint32_t>{29, 30} &&
                        !editor.document()->can_redo(),
                    "Duplication branch reused abandoned IDs or retained Redo");
            number("mesh:grid_spacing", ".25");
            click("button:Snap selection to grid");
            const std::array<std::uint32_t, 6> branch_vertices{23, 24, 25, 26, 27, 28};
            expected.snap_vertices(branch_vertices, .25, {true, false, false});
            const auto final_source = expected.document;
            require(editor.document()->source().document == final_source,
                    "Branched duplicate grid placement differs");
            click("mesh:selection_mode");
            key(ImGuiKey_Home);
            key(ImGuiKey_DownArrow);
            key(ImGuiKey_Enter);
            click("mesh:edge:23:24");
            click("mesh:grid_x");
            click("mesh:grid_y");
            number("mesh:grid_spacing", "1");
            click("button:Snap selection to grid");
            auto edge_expected = EditableMeshSource{final_source};
            edge_expected.snap_vertices(std::array<std::uint32_t, 2>{23, 24}, 1,
                                        {false, true, false});
            require(editor.document()->source().document == edge_expected.document,
                    "Edge grid scope changed unrelated vertices");
            click("button:Undo");
            click("mesh:selection_mode");
            key(ImGuiKey_Home);
            key(ImGuiKey_Enter);
            auto* parent = ImGui::FindWindowByName("###Editable Mesh");
            for (auto* child : ImGui::GetCurrentContext()->Windows)
                if (child->ParentWindow == parent &&
                    std::string_view(child->Name).find("/Mesh elements_") != std::string_view::npos)
                    ImGui::SetScrollY(child, 0);
            frame();
            frame();
            click("mesh:vertex:1");
            const auto vertex_revision = editor.document()->revision();
            click("button:Snap selection to grid");
            require(editor.document()->revision() == vertex_revision &&
                        editor.document()->source().document == final_source,
                    "Invalid Vertex-scope grid snap changed source/history");
            click("mesh:selection_mode");
            key(ImGuiKey_End);
            key(ImGuiKey_Enter);
            click("mesh:face:29");
            click("mesh:preview_transform");
            disabled("button:Duplicate selected faces");
            disabled("button:Snap selection to grid");
            disabled("mesh:duplicate_offset");
            disabled("mesh:grid_spacing");
            key(ImGuiKey_Escape);
            locked = true;
            disabled("button:Duplicate selected faces");
            disabled("button:Snap selection to grid");
            locked = false;
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty() && bool(editor.take_catalog()), "Placement publication failed");
            const auto target_profile = desktop_texture_target();
            const auto mesh_id = editor.document()->source().asset();
            const auto expected_mesh = editor.document()->source().cook();
            const auto output = root.parent_path() / ("copy-export-" + AssetId::generate().str());
            const std::array roots{mesh_id};
            package_runtime_content(root, output, roots,
                                    {target_profile.platform, target_profile.backend});
            const auto relocated =
                root.parent_path() / ("copy-relocated-" + AssetId::generate().str());
            std::filesystem::rename(output, relocated);
            const auto offline = root / "Assets/Copies.mesh.offline";
            std::filesystem::rename(root / "Assets/Copies.mesh.json", offline);
            require(!std::filesystem::exists(relocated / "Assets/Copies.mesh.json"),
                    "Duplicate source leaked into export");
            const auto shipped = std::make_shared<const AssetCatalog>(
                open_runtime_content(relocated, {target_profile.platform, target_profile.backend}));
            ResourcePool<MeshAsset> pool;
            const auto ticket =
                asset_detail::request_model_mesh(pool, relocated, shipped, {mesh_id});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Relocated duplicate Mesh cannot load");
            const auto resource = pool.acquire(ticket);
            require(encode_mesh(resource->mesh) == encode_mesh(expected_mesh),
                    "Placement export differs from authored cook");
            std::filesystem::rename(offline, root / "Assets/Copies.mesh.json");
            require(scene.document() == authored, "Placement changed Scene source");
            editor.request_close();
            frame();
            editor.open(project, "Assets/Copies.mesh.json");
            frame();
            require(editor.document()->source().document == final_source,
                    "Placement source save/reopen differs");
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual Model duplication/grid100/150/200% "
                     "history/identity/publication/source-offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
