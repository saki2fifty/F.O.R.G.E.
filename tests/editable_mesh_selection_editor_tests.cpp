#include "editor/editable_mesh_editor.hpp"
#include <algorithm>
#include <array>
#include <imgui_internal.h>
#include <iostream>
#include <thread>
#include <tuple>
using namespace forge;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
EditableMeshSource grid() {
    auto mesh = EditableMeshSource::create_cube(AssetId::generate());
    mesh.document["vertices"] = Json::array();
    mesh.document["faces"] = Json::array();
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x)
            mesh.document["vertices"].push_back(
                {{"id", y * 64 + x + 1}, {"position", {double(x), double(y), 0.}}});
    unsigned id = 4097;
    for (unsigned y = 0; y < 63; ++y)
        for (unsigned x = 0; x < 63; ++x) {
            auto corners = Json::array();
            for (auto [cx, cy] : std::array<std::array<unsigned, 2>, 4>{
                     {{x, y}, {x + 1, y}, {x + 1, y + 1}, {x, y + 1}}})
                corners.push_back(
                    {{"vertex", cy * 64 + cx + 1}, {"uv", {double(cx) / 63, double(cy) / 63}}});
            mesh.document["faces"].push_back({{"id", id++}, {"corners", corners}});
        }
    mesh.document["next_id"] = id;
    mesh.validate();
    return mesh;
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need vertex selection UI scratch root");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto source = EditableMeshSource::create_cube(AssetId::generate());
            // Triangles allow a small subset deformation without making a quad nonplanar.
            source.triangulate_faces(std::array<std::uint32_t, 6>{9, 10, 11, 12, 13, 14});
            source.document["extension"] = {{"retain", 42}};
            atomic_write(root / "Assets/Cube.mesh.json", source.document.dump(2));
            {
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
                io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
                ui::interface_scale = scale;
                unsigned char* pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                ui::EditorUiContext context;
                ui::ContextScope scope(context);
                test::observe_ui = true;
                EditableMeshEditor editor;
                editor.open(project, "Assets/Cube.mesh.json");
                bool locked = false;
                ImVec2 window_size{1600, 900};
                std::string message;
                auto frame = [&] {
                    test::ui_targets.clear();
                    editor.poll(project, message);
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
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
                    require(test::ui_targets.contains(name), "Missing selection target: " + name);
                    auto box = test::ui_targets.at(name);
                    if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                        auto* window = ImGui::FindWindowByName("###Editable Mesh");
                        require(window, "Mesh window missing");
                        ImGui::SetScrollY(window, window->Scroll.y +
                                                      (box.minimum.y + box.maximum.y -
                                                       box.clip_minimum.y - box.clip_maximum.y) *
                                                          .5f);
                        frame();
                        frame();
                        require(test::ui_targets.contains(name),
                                "Selection target lost while scrolling: " + name);
                        box = test::ui_targets.at(name);
                    }
                    require(box.enabled && box.minimum.x >= box.clip_minimum.x &&
                                box.maximum.x <= box.clip_maximum.x &&
                                box.minimum.y >= box.clip_minimum.y &&
                                box.maximum.y <= box.clip_maximum.y,
                            "Selection target clipped/disabled: " + name);
                    return box;
                };
                auto click = [&](const std::string& name) {
                    if (name.starts_with("mesh:vertex:"))
                        target("mesh:vertex_list");
                    const auto box = target(name);
                    const ImVec2 at{(box.minimum.x + box.maximum.x) * .5f,
                                    (box.minimum.y + box.maximum.y) * .5f};
                    mouse(at, false);
                    mouse(at, true);
                    mouse(at, false);
                    frame();
                };
                auto key = [&](ImGuiKey code) {
                    io.AddKeyEvent(code, true);
                    frame();
                    io.AddKeyEvent(code, false);
                    frame();
                };
                auto number = [&](const std::string& name, const char* text) {
                    const auto box = target(name);
                    const ImVec2 at{box.minimum.x + 20, (box.minimum.y + box.maximum.y) * .5f};
                    mouse(at, false);
                    mouse(at, true);
                    mouse(at, false);
                    io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    io.AddKeyEvent(ImGuiKey_A, true);
                    frame();
                    io.AddKeyEvent(ImGuiKey_A, false);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    io.AddInputCharactersUTF8(text);
                    frame();
                    key(ImGuiKey_Enter);
                };
                unsigned count_checks = 0;
                auto count = [&](std::size_t value) {
                    ++count_checks;
                    frame();
                    require(test::ui_targets.contains("mesh:vertex_count:" + std::to_string(value)),
                            "Visible selected-vertex count differs: " + std::to_string(value) +
                                " check=" + std::to_string(count_checks) +
                                " scale=" + std::to_string(scale) + " source=" +
                                (editor.document() ? path_utf8(editor.document()->locator())
                                                   : "<closed>"));
                };
                auto unchanged = [&] {
                    require(editor.document()->source().document == source.document &&
                                !editor.can_undo(),
                            "Selection changed source/history");
                };
                auto apply = [&](const std::set<std::uint32_t>& ids) {
                    auto expected = source;
                    const std::vector<std::uint32_t> group(ids.begin(), ids.end());
                    expected.transform_vertices(group, {.125, 0, 0}, {}, {1, 1, 1});
                    click("button:Apply transform");
                    require(editor.document()->source().document == expected.document &&
                                editor.can_undo(),
                            "Selection did not reach the existing exact source transform/history "
                            "owner");
                    editor.undo();
                    frame();
                    unchanged();
                };
                frame();
                frame();
                click("mesh:selection_mode");
                key(ImGuiKey_Home);
                key(ImGuiKey_Enter);
                count(0);
                unchanged();
                number("mesh:model_move", "0.125");
                click("button:Select all vertices");
                count(8);
                unchanged();
                apply({1, 2, 3, 4, 5, 6, 7, 8});
                click("button:Clear vertex selection");
                count(0);
                unchanged();
                require(!test::ui_targets.at("button:Apply transform").enabled,
                        "Empty vertex selection retained an enabled transform action");
                unchanged();
                click("mesh:vertex:1");
                count(1);
                click("button:Invert vertex selection");
                count(7);
                unchanged();
                apply({2, 3, 4, 5, 6, 7, 8});
                click("button:Invert vertex selection");
                count(1);
                click("button:Clear vertex selection");
                click("mesh:vertex_box_mode");
                auto rectangle = [&] {
                    const auto box = target("mesh:model_canvas");
                    const ImVec2 a{box.minimum.x + 1, box.minimum.y + 1};
                    const ImVec2 b{(box.minimum.x + box.maximum.x) * .5f, box.maximum.y - 1};
                    std::set<std::uint32_t> hits;
                    for (unsigned id = 1; id <= 8; ++id) {
                        const auto point =
                            test::ui_targets.at("mesh:projected_vertex:" + std::to_string(id));
                        const ImVec2 center{(point.minimum.x + point.maximum.x) * .5f,
                                            (point.minimum.y + point.maximum.y) * .5f};
                        if (center.x >= a.x && center.x <= b.x && center.y >= a.y &&
                            center.y <= b.y)
                            hits.insert(id);
                    }
                    require(!hits.empty() && hits.size() < 8,
                            "Independent displayed-point rectangle oracle is not a subset");
                    return std::tuple{a, b, hits};
                };
                auto [a, b, hits] = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                unchanged(); // Held preview has no authoring/history changes.
                mouse(b, false);
                count(hits.size());
                unchanged();
                apply(hits);
                click("button:Clear vertex selection");
                std::tie(a, b, hits) = rectangle();
                mouse(b, false);
                mouse(b, true);
                mouse(a, false); // Reverse direction, sparse release.
                count(hits.size());
                unchanged();
                apply(hits);
                // A no-move click preserves the previous set and exact source.
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(a, false);
                count(hits.size());
                apply(hits);
                click("button:Select all vertices");
                count(8);
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                key(ImGuiKey_Escape);
                mouse(b, false);
                count(8);
                unchanged();
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                locked = true;
                frame();
                mouse(b, false);
                locked = false;
                frame();
                count(8);
                unchanged();
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                io.AddFocusEvent(false);
                frame();
                mouse(b, false);
                io.AddFocusEvent(true);
                frame();
                count(8);
                unchanged();
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                window_size.x = 1400;
                frame();
                mouse(b, false);
                window_size.x = 1600;
                frame();
                count(8);
                unchanged();
                // Ctrl union retains a vertex outside the projected half.
                click("mesh:vertex_box_mode");
                click("button:Clear vertex selection");
                std::tie(a, b, hits) = rectangle();
                unsigned outside = 1;
                while (hits.contains(outside))
                    ++outside;
                const auto marker =
                    test::ui_targets.at("mesh:projected_vertex:" + std::to_string(outside));
                const ImVec2 at{(marker.minimum.x + marker.maximum.x) * .5f,
                                (marker.minimum.y + marker.maximum.y) * .5f};
                mouse(at, false);
                mouse(at, true);
                mouse(at, false);
                count(1);
                click("mesh:vertex_box_mode");
                std::tie(a, b, hits) = rectangle();
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                frame();
                mouse(a, false);
                mouse(a, true);
                mouse(b, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                frame();
                hits.insert(outside);
                count(hits.size());
                apply(hits);
                // Existing public history actions invalidate an in-flight projection.
                click("button:Select all vertices");
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                require(editor.can_redo(), "Expected retained transform history branch");
                editor.redo();
                frame();
                mouse(b, false);
                count(8);
                editor.undo();
                frame();
                unchanged();
                // Persist an actual edited group, not only the initial imported geometry.
                click("button:Select all vertices");
                auto persisted = source;
                persisted.transform_vertices(std::array<std::uint32_t, 8>{1, 2, 3, 4, 5, 6, 7, 8},
                                             {.125, 0, 0}, {}, {1, 1, 1});
                click("button:Apply transform");
                require(editor.document()->source().document == persisted.document,
                        "Persisted selected group differs before saving");
                // Publication cancels held selection before an asynchronous worker can finish.
                click("button:Select all vertices");
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                editor.request_save();
                frame();
                mouse(b, false);
                const auto publication_deadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(20);
                while (editor.pending() &&
                       std::chrono::steady_clock::now() < publication_deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    frame();
                }
                require(!editor.pending() && !editor.dirty(),
                        "Selection source publication failed: pending=" +
                            std::to_string(editor.pending()) +
                            " dirty=" + std::to_string(editor.dirty()) + " " + message);
                count(8);
                require(scene.document() == scene_before, "Selection changed Scene owner");
                std::tie(a, b, hits) = rectangle();
                mouse(a, false);
                mouse(a, true);
                mouse(b, true);
                editor.request_close();
                frame();
                mouse(b, false);
                require(!editor.is_open(), "Clean held-selection close failed");
                editor.open(project, "Assets/Cube.mesh.json");
                if (!editor.is_open()) {
                    for (const auto& problem : context.problems.items())
                        std::cerr << problem.key << ": " << problem.text << '\n';
                    require(false, "Published source could not reopen");
                }
                frame();
                count(0);
                require(editor.document()->source().document == persisted.document,
                        "Published/reopened edited geometry differs");
                auto* mesh_window = ImGui::FindWindowByName("###Editable Mesh");
                require(mesh_window && mesh_window->HasCloseButton, "Mesh close button missing");
                const auto titlebar = mesh_window->TitleBarRect();
                const ImVec2 close_at{titlebar.Max.x - ImGui::GetStyle().FramePadding.x -
                                          mesh_window->FontWindowScale * ImGui::GetFontSize() * .5f,
                                      (titlebar.Min.y + titlebar.Max.y) * .5f};
                mouse(close_at, false);
                mouse(close_at, true);
                mouse(close_at, false);
                require(!editor.is_open(), "Actual clean Mesh window close failed");
                // Near-bound source: clipped rows, complete selection, source unchanged.
                const auto large = grid();
                // Compact formatting keeps this near-bound fixture inside the existing2MiB source
                // cap.
                atomic_write(root / "Assets/Grid.mesh.json", large.document.dump());
                editor.open(project, "Assets/Grid.mesh.json");
                frame();
                frame();
                count(0);
                target("mesh:vertex_list");
                auto rows = [&] {
                    return std::count_if(
                        test::ui_targets.begin(), test::ui_targets.end(),
                        [](const auto& entry) { return entry.first.starts_with("mesh:vertex:"); });
                };
                require(rows() > 0 && rows() < 20, "Vertex clipper submitted hidden4096 rows");
                ImGuiWindow* child = nullptr;
                const auto* parent = ImGui::FindWindowByName("###Editable Mesh");
                for (auto* window : ImGui::GetCurrentContext()->Windows)
                    if (window->ParentWindow == parent &&
                        std::string(window->Name).find("Mesh elements") != std::string::npos)
                        child = window;
                require(child, "Clipped vertex list window missing");
                ImGui::SetScrollY(child, 0);
                frame();
                frame();
                require(test::ui_targets.contains("mesh:vertex:1") && rows() < 20,
                        "Clipped list cannot reach the first stable vertex ID");
                ImGui::SetScrollY(child, child->ScrollMax.y);
                frame();
                frame();
                require(test::ui_targets.contains("mesh:vertex:4096") && rows() < 20,
                        "Clipped list cannot reach the last stable vertex ID");
                click("button:Select all vertices");
                count(4096);
                require(editor.document()->source().document == large.document &&
                            !editor.can_undo(),
                        "4096 selection changed source/history");
                target("mesh:model_canvas");
                test::observe_ui = false;
                std::vector<double> timings;
                for (unsigned i = 0; i < 12; ++i) {
                    const auto start = std::chrono::steady_clock::now();
                    frame();
                    if (i >= 3)
                        timings.push_back(std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - start)
                                              .count());
                }
                std::sort(timings.begin(), timings.end());
                std::cout << "scale=" << scale
                          << " vertices=4096 idle_model_frame_cpu_ms=" << timings[4]
                          << " (fixture CPU; no GPU/physical FPS claim)\n";
                require(editor.document()->source().document == large.document &&
                            !editor.can_undo(),
                        "Idle Model frames edited4096 source");
                test::observe_ui = true;
                editor.request_close();
                frame();
                click("button:Discard");
                require(!editor.is_open(), "Unpublished large-source close failed");
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
            std::filesystem::remove_all(root);
        }
        ui::interface_scale = 1;
        std::cout << "Actual vertex "
                     "selection/rectangle/Ctrl/sparse-release/cancel/transform/history/publication/"
                     "reopen at65/100/200% passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
