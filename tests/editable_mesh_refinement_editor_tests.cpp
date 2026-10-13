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
        require(argc == 2, "Need Model topology refinement UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = EditableMeshSource::create_cube(AssetId::generate());
            const auto original = source.document;
            atomic_write(root / "Assets/Refinement.mesh.json", original.dump(2));
            auto conflict_source = source;
            conflict_source.document["asset_id"] = AssetId::generate();
            conflict_source.document["vertices"][0]["opaque"] = "cannot interpolate";
            atomic_write(root / "Assets/Conflict.mesh.json", conflict_source.document.dump(2));
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            const auto authored = scene.document();
            EditableMeshEditor editor;
            editor.open(project, "Assets/Conflict.mesh.json");
            require(editor.document(), "Refinement Mesh did not open");
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
                require(test::ui_targets.contains(id), "Missing topology refinement control " + id);
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
                        "Clipped/disabled topology refinement control " + id);
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
            auto face_pair = [&] {
                click("mesh:face:9");
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                click("mesh:face:13");
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
            };
            std::string stage = "empty face selection";
            auto disabled = [&](const std::string& id) {
                frame();
                require(
                    test::ui_targets.contains(id) && !test::ui_targets.at(id).enabled,
                    "Expected disabled refinement control " + id + " at " + stage +
                        "; present=" + std::to_string(test::ui_targets.contains(id)) +
                        "; selected=" + std::to_string(editor.observed_face_selection().size()));
            };
            frame();
            frame();
            click("mesh:model_tab");
            const auto conflicting = editor.document()->source().document;
            const auto failure_revision = editor.document()->revision();
            click("button:Subdivide Mesh");
            require(editor.document()->revision() == failure_revision &&
                        editor.document()->source().document == conflicting &&
                        !editor.document()->can_undo(),
                    "Failed refinement changed source/history");
            editor.request_close();
            frame();
            click("button:Discard");
            require(!editor.is_open(),
                    "Unpublished conflict source did not close through its guard");
            editor.open(project, "Assets/Refinement.mesh.json");
            frame();
            click("mesh:model_tab");
            require(editor.document()->source().asset() == source.asset(),
                    "Guard left the wrong Mesh open");
            click("button:Clear face selection");
            disabled("button:Split selected faces at centers");
            face_pair();
            click("mesh:uv_tab");
            require(!editor.observed_uv_selection().empty(), "UV precondition not selected");
            click("mesh:model_tab");
            face_pair();
            click("button:Split selected faces at centers");
            auto expected = EditableMeshSource{original};
            const auto children =
                expected.split_faces_at_centers(std::array<std::uint32_t, 2>{9, 13});
            const auto centered = expected.document;
            require(editor.document()->source().document == centered &&
                        editor.observed_face_selection() ==
                            std::set<std::uint32_t>(children.begin(), children.end()) &&
                        editor.observed_uv_selection().empty(),
                    "Center split differs or leaves stale UV selection");
            click("button:Undo");
            auto reserved = original;
            reserved["next_id"] = 23;
            require(editor.document()->source().document == reserved,
                    "Center split Undo did not preserve reservation");
            click("button:Redo");
            require(editor.document()->source().document == centered, "Center split Redo differs");
            click("button:Undo");
            click("mesh:selection_mode");
            key(ImGuiKey_Home);
            key(ImGuiKey_Enter);
            stage = "Vertex selection mode";
            disabled("button:Split selected faces at centers");
            click("button:Subdivide Mesh");
            expected = EditableMeshSource{reserved};
            const auto refined = expected.subdivide();
            const auto divided = expected.document;
            require(editor.document()->source().document == divided &&
                        editor.observed_face_selection() ==
                            std::set<std::uint32_t>(refined.begin(), refined.end()),
                    "Whole Mesh subdivision/face selection differs from Vertex mode");
            click("button:Undo");
            reserved["next_id"] = divided["next_id"];
            require(editor.document()->source().document == reserved,
                    "Subdivision Undo changed original geometry or allocator");
            click("button:Redo");
            require(editor.document()->source().document == divided, "Subdivision Redo differs");
            click("mesh:face:9");
            click("mesh:preview_transform");
            stage = "transform preview";
            disabled("button:Split selected faces at centers");
            disabled("button:Subdivide Mesh");
            key(ImGuiKey_Escape);
            locked = true;
            stage = "locked";
            disabled("button:Split selected faces at centers");
            disabled("button:Subdivide Mesh");
            locked = false;
            // A discarded subdivision branch cannot recycle its generated IDs.
            click("button:Undo");
            click("mesh:face:9");
            click("button:Split selected faces at centers");
            expected = EditableMeshSource{reserved};
            expected.split_faces_at_centers(std::array<std::uint32_t, 1>{9});
            const auto final_source = expected.document;
            require(editor.document()->source().document == final_source &&
                        !editor.document()->can_redo(),
                    "Refinement branch reused IDs or retained stale Redo");
            click("mesh:brush_tool");
            key(ImGuiKey_Home);
            key(ImGuiKey_DownArrow);
            key(ImGuiKey_Enter);
            target("mesh:model_canvas");
            const auto stamp = target("mesh:projected_vertex:8");
            const auto stroke_revision = editor.document()->revision();
            mouse(stamp, false);
            mouse(stamp, true);
            stage = "held brush stroke";
            disabled("button:Split selected faces at centers");
            disabled("button:Subdivide Mesh");
            key(ImGuiKey_Escape);
            mouse(stamp, false);
            require(editor.document()->revision() == stroke_revision &&
                        editor.document()->source().document == final_source,
                    "Cancelled brush stroke changed refined source");
            click("mesh:brush_tool");
            key(ImGuiKey_Home);
            key(ImGuiKey_Enter);
            editor.request_save();
            frame();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                frame();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            frame();
            require(!editor.dirty() && bool(editor.take_catalog()),
                    "Refinement publication failed");
            const auto target_profile = desktop_texture_target();
            const auto mesh_id = editor.document()->source().asset();
            const auto expected_mesh = editor.document()->source().cook();
            const auto output =
                root.parent_path() / ("refinement-export-" + AssetId::generate().str());
            const std::array roots{mesh_id};
            package_runtime_content(root, output, roots,
                                    {target_profile.platform, target_profile.backend});
            const auto relocated =
                root.parent_path() / ("refinement-relocated-" + AssetId::generate().str());
            std::filesystem::rename(output, relocated);
            const auto offline = root / "Assets/Refinement.mesh.offline";
            std::filesystem::rename(root / "Assets/Refinement.mesh.json", offline);
            require(!std::filesystem::exists(relocated / "Assets/Refinement.mesh.json"),
                    "Refinement source leaked into export");
            const auto shipped = std::make_shared<const AssetCatalog>(
                open_runtime_content(relocated, {target_profile.platform, target_profile.backend}));
            ResourcePool<MeshAsset> pool;
            const auto ticket =
                asset_detail::request_model_mesh(pool, relocated, shipped, {mesh_id});
            require(pool.wait(ticket, std::chrono::seconds(5)) && bool(pool.acquire(ticket)),
                    "Relocated refinement Mesh cannot load");
            const auto resource = pool.acquire(ticket);
            require(encode_mesh(resource->mesh) == encode_mesh(expected_mesh),
                    "Refinement export differs from authored cook");
            std::filesystem::rename(offline, root / "Assets/Refinement.mesh.json");
            require(scene.document() == authored, "Refinement changed Scene source");
            editor.request_close();
            frame();
            editor.open(project, "Assets/Refinement.mesh.json");
            frame();
            require(editor.document()->source().document == final_source,
                    "Refinement source save/reopen differs");
            editor.request_close();
            frame();
            ImGui::DestroyContext();
        }
        std::cout << "Actual Model topology refinement100/150/200% "
                     "history/identity/publication/source-offline export passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
