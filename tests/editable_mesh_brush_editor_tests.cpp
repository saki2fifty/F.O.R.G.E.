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
ImVec2 source_point(const Json& source, ImVec2 minimum, ImVec2 maximum, bool angled) {
    const double yaw = 40 * std::numbers::pi / 180, pitch = 25 * std::numbers::pi / 180;
    double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9, max_x = -1e9;
    std::array<double, 2> chosen{};
    for (const auto& v : source.at("vertices")) {
        const auto p = v.at("position").get<std::array<double, 3>>();
        const auto x = std::cos(yaw) * p[0] - std::sin(yaw) * p[2];
        const auto depth = std::sin(yaw) * p[0] + std::cos(yaw) * p[2];
        const auto y = -(std::cos(pitch) * p[1] - std::sin(pitch) * depth);
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
        const auto score = angled ? p[0] + p[1] + p[2] : p[0];
        if (score > max_x) {
            max_x = score;
            chosen = {x, y};
        }
    }
    const double fit = std::min((maximum.x - minimum.x - 40) / std::max(.01, right - left),
                                (maximum.y - minimum.y - 40) / std::max(.01, bottom - top));
    return {float((minimum.x + maximum.x) / 2 + (chosen[0] - (left + right) / 2) * fit),
            float((minimum.y + maximum.y) / 2 + (chosen[1] - (top + bottom) / 2) * fit)};
}
void mirrored_values(const Json& original, const Json& changed, bool paint) {
    unsigned affected = 0, left = 0, right = 0;
    for (std::size_t i = 0; i < original.at("vertices").size(); ++i) {
        const auto p = original["vertices"][i]["position"].get<std::array<double, 3>>();
        const auto& v = changed["vertices"][i];
        const bool edited =
            paint ? v.contains("color") : v["position"] != original["vertices"][i]["position"];
        if (edited) {
            ++affected;
            if (p[0] < -.1)
                ++left;
            if (p[0] > .1)
                ++right;
        }
        std::size_t match = original.at("vertices").size();
        for (std::size_t j = 0; j < original.at("vertices").size(); ++j) {
            const auto q = original["vertices"][j]["position"].get<std::array<double, 3>>();
            if (std::abs(p[0] + q[0]) < 1e-12 && std::abs(p[1] - q[1]) < 1e-12 &&
                std::abs(p[2] - q[2]) < 1e-12) {
                match = j;
                break;
            }
        }
        require(match < original.at("vertices").size(),
                "Sphere fixture lost mirror correspondence");
        if (paint) {
            const auto a = v.value("color", Json::array({1, 1, 1, 1}));
            const auto b = changed["vertices"][match].value("color", Json::array({1, 1, 1, 1}));
            for (unsigned c = 0; c < 4; ++c)
                require(std::abs(a[c].get<double>() - b[c].get<double>()) < 1e-9,
                        "Paint axis control did not mirror color");
            require(v["position"] == original["vertices"][i]["position"], "Paint moved geometry");
        }
    }
    require(affected > 0 && left > 0 && right > 0, "Mirrored stroke omitted either side");
    require(changed.at("faces") == original.at("faces") &&
                changed.at("next_id") == original.at("next_id"),
            "Brush changed corner UV/topology/IDs");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need brush UI scratch root");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            const auto source = EditableMeshSource::create_sculpt_sphere(AssetId::generate());
            atomic_write(root / "Assets/Sphere.mesh.json", source.document.dump(2));
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
                editor.open(project, "Assets/Sphere.mesh.json");
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
                    require(test::ui_targets.contains(name), "Missing brush target");
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
                auto key = [&](ImGuiKey k) {
                    io.AddKeyEvent(k, true);
                    frame();
                    io.AddKeyEvent(k, false);
                    frame();
                };
                auto tool = [&](bool paint) {
                    click("mesh:brush_tool");
                    key(ImGuiKey_Home);
                    key(ImGuiKey_DownArrow);
                    if (paint)
                        key(ImGuiKey_DownArrow);
                    key(ImGuiKey_Enter);
                };
                bool angled = false;
                auto point = [&] {
                    const auto box = target("mesh:model_canvas");
                    return source_point(editor.document()->source().document, box.minimum,
                                        box.maximum, angled);
                };
                frame();
                frame();
                click("mesh:uv_tab");
                require(!test::ui_targets.contains("mesh:brush_tool"),
                        "UV workspace unexpectedly exposed Model brush controls");
                click("mesh:model_tab");
                tool(false);
                click("mesh:brush_mirror_x");
                const auto baseline = editor.document()->source().document;
                auto expected_sculpt = source;
                std::array<double, 3> center{};
                double largest_x = -1e9;
                for (const auto& vertex : baseline.at("vertices")) {
                    const auto p = vertex.at("position").get<std::array<double, 3>>();
                    if (p[0] > largest_x) {
                        largest_x = p[0];
                        center = p;
                    }
                }
                auto reflected = center;
                reflected[0] = -reflected[0];
                // Old no-symmetry API with explicit stamps is an independent gesture oracle.
                // The triangulated sphere's paired averaged normals need not mirror exactly.
                const std::array stamps{MeshBrushSample{center, 1}, MeshBrushSample{reflected, 1}};
                expected_sculpt.sculpt(stamps, .25, double(.05f));
                auto start = point();
                mouse(start, false);
                mouse(start, true);
                frame();
                require(editor.document()->source().document == baseline &&
                            !editor.document()->can_undo(),
                        "Held sculpt changed source/history");
                require(test::ui_targets.contains("mesh:brush_mirror_x") &&
                            !test::ui_targets.at("mesh:brush_mirror_x").enabled,
                        "Active stroke settings remain editable");
                key(ImGuiKey_Escape);
                mouse(start, false);
                frame();
                require(editor.document()->source().document == baseline,
                        "Cancelled sculpt committed");
                start = point();
                mouse(start, false);
                mouse(start, true);
                mouse(start, false);
                frame();
                mirrored_values(baseline, editor.document()->source().document, false);
                require(
                    editor.document()->source().document == expected_sculpt.document,
                    "Actual Sculpt control/stamp differs from explicit-reflection source oracle");
                click("button:Undo");
                require(editor.document()->source().document == baseline,
                        "Mirrored sculpt was not one Undo step");
                click("button:Redo");
                mirrored_values(baseline, editor.document()->source().document, false);
                require(
                    editor.document()->source().document == expected_sculpt.document,
                    "Actual Sculpt control/stamp differs from explicit-reflection source oracle");
                click("button:Undo");
                tool(true);
                click("mesh:brush_mirror_y");
                click("mesh:brush_mirror_z");
                angled = true;
                auto expected_paint = source;
                std::array<double, 3> paint_center{};
                double score = -1e9;
                for (const auto& vertex : baseline.at("vertices")) {
                    const auto p = vertex.at("position").get<std::array<double, 3>>();
                    if (p[0] + p[1] + p[2] > score) {
                        score = p[0] + p[1] + p[2];
                        paint_center = p;
                    }
                }
                std::vector<MeshBrushSample> paint_stamps;
                for (unsigned mask = 0; mask < 8; ++mask) {
                    auto reflected = paint_center;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        if (mask & (1u << axis))
                            reflected[axis] = -reflected[axis];
                    paint_stamps.push_back({reflected, 1});
                }
                expected_paint.paint_color(paint_stamps, .25,
                                           {double(.9f), double(.2f), double(.1f), 1}, double(.8f));
                start = point();
                mouse(start, false);
                frame();
                unsigned ghost_vertices = 0;
                const auto* drawn = ImGui::GetDrawData();
                for (int list = 0; list < drawn->CmdListsCount; ++list)
                    for (const auto& vertex : drawn->CmdLists[list]->VtxBuffer)
                        if (vertex.col == IM_COL32(245, 167, 109, 150))
                            ++ghost_vertices;
                require(ghost_vertices > 0, "Mirrored hover rings were not drawn");
                mouse(start, false);
                mouse(start, true);
                locked = true;
                frame();
                mouse(start, false);
                locked = false;
                frame();
                require(editor.document()->source().document == baseline,
                        "Lock committed brush stroke");
                start = point();
                mouse(start, false);
                mouse(start, true);
                mouse(start, false);
                frame();
                const auto painted = editor.document()->source().document;
                mirrored_values(baseline, painted, true);
                require(painted == expected_paint.document,
                        "Actual XYZ paint axes differ from explicit stamp oracle");
                require(editor.document()->source().cook().lods[0].parts[0].find("COLOR_0"),
                        "Painted source lacks cooked colors");
                start = point();
                mouse(start, false);
                mouse(start, true);
                editor.undo();
                frame();
                mouse(start, false);
                frame();
                require(editor.document()->source().document == baseline,
                        "Undo during stroke allowed stale release");
                editor.redo();
                frame();
                require(editor.document()->source().document == painted,
                        "Redo lost mirrored paint");
                click("button:Save & Publish");
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    frame();
                }
                require(!editor.pending() && !editor.dirty(), "Mirrored paint publication failed");
                editor.request_close();
                frame();
                require(!editor.is_open(), "Published brush document did not close");
                editor.open(project, "Assets/Sphere.mesh.json");
                frame();
                require(editor.document()->source().document == painted,
                        "Brush save/reopen lost values");
                // A release can be the only frame that reports the final pointer position.
                // Compare sparse events to the identical drag with an observed held endpoint.
                start = point();
                auto canvas = target("mesh:model_canvas");
                auto end = source_point(editor.document()->source().document, canvas.minimum,
                                        canvas.maximum, false);
                mouse(start, false);
                mouse(start, true);
                mouse(end, false);
                frame();
                const auto sparse = editor.document()->source().document;
                require(sparse != painted, "Sparse brush release did not apply a stroke");
                editor.undo();
                frame();
                require(editor.document()->source().document == painted,
                        "Sparse brush Undo differs");
                // Undo to saved content reveals the publication reminder. Resolve the
                // current canvas again so both trials target the same source points.
                start = point();
                canvas = target("mesh:model_canvas");
                end = source_point(editor.document()->source().document, canvas.minimum,
                                   canvas.maximum, false);
                mouse(start, false);
                mouse(start, true);
                mouse(end, true);
                mouse(end, false);
                frame();
                if (editor.document()->source().document != sparse) {
                    for (std::size_t i = 0; i < sparse.at("vertices").size(); ++i)
                        if (sparse["vertices"][i] !=
                            editor.document()->source().document["vertices"][i])
                            throw std::runtime_error(
                                "Release sparse/held at scale " + std::to_string(scale) +
                                " vertex " + std::to_string(i) + " " +
                                sparse["vertices"][i].dump() + " / " +
                                editor.document()->source().document["vertices"][i].dump());
                }
                editor.undo();
                frame();
                require(scene.document() == scene_before, "Brush changed Scene owner/history");
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
            std::filesystem::remove_all(root);
        }
        ui::interface_scale = 1;
        std::cout << "Actual sculpt/paint symmetry controls, "
                     "held/cancel/lock/Undo/publication/reopen at65/100/200% passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
