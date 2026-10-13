#include "editor/material_graph_editor.hpp"
#include "material_graph_layout_tests.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
struct Input {
    std::function<void()> frame;
    ImVec2 point(const std::string& id) {
        check(test::ui_targets.contains(id), "Missing graph UI " + id);
        const auto target = test::ui_targets.at(id);
        check(target.enabled, "Disabled graph UI " + id);
        const ImVec2 p{(target.minimum.x + target.maximum.x) / 2,
                       (target.minimum.y + target.maximum.y) / 2};
        check(p.x >= target.clip_minimum.x && p.x <= target.clip_maximum.x &&
                  p.y >= target.clip_minimum.y && p.y <= target.clip_maximum.y,
              "Clipped graph UI " + id);
        return p;
    }
    void mouse(ImVec2 p, bool down) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, down);
        frame();
    }
    void click(const std::string& id) {
        frame();
        const auto p = point(id);
        mouse(p, false);
        mouse(p, true);
        mouse(p, false);
        frame();
    }
    void key(ImGuiKey key, bool down) {
        ImGui::GetIO().AddKeyEvent(key, down);
        frame();
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need graph layout scratch root");
        test::material_graph_layout_tests();
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root, true);
            auto document =
                MaterialGraphDocument::create(project.writer_guard(), "Assets/Layout.shader.json");
            document->edit(document->revision(), "Fixture layout", [](auto& source) {
                auto& graph = source["graph"];
                graph["nodes"][0]["position"] = {0, 0};
                graph["nodes"][1]["position"] = {750, 0};
                graph["nodes"].push_back(create_material_graph_node("constant", {375, 170}));
                graph["nodes"].back()["data"]["type"] = "scalar";
                graph["nodes"].back()["data"]["value"] = {0.5};
                graph["opaque"] = {{"preserve", 72}};
                auto nested = graph_tests::threshold_graph("step", "scalar", {{.5}, {.25}});
                std::set<GraphNodeId> pure;
                for (const auto& n : nested["nodes"])
                    if (n["type"] != "surface")
                        pure.insert(n["id"].get<GraphNodeId>());
                extract_material_graph_function(nested, pure, "Arrangement function");
                graph["functions"] = nested["functions"];
            });
            document->save();
            const auto original = document->source().document;
            std::set<GraphNodeId> all;
            for (const auto& n : original["graph"]["nodes"])
                all.insert(n["id"].get<GraphNodeId>());
            const auto first = original["graph"]["nodes"][0]["id"].get<GraphNodeId>();
            const auto revision = document->revision();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2400, 2000};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int w, h;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            io.FontGlobalScale = scale;
            ImGui::GetStyle().ScaleAllSizes(scale);
            ui::interface_scale = scale;
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            MaterialGraphCanvas canvas;
            bool locked = false;
            ImVec2 window_size{2300, 1800};
            MaterialGraphCanvas::Edit edit = [&](std::string label, const auto& op) {
                document->edit(document->revision(), std::move(label),
                               [&](auto& s) { op(s["graph"]); });
            };
            Input input;
            input.frame = [&] {
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
                ImGui::Begin("Graph arrangement");
                const auto g = document->source().document["graph"];
                canvas.draw(g, document->revision(), edit, locked);
                ImGui::End();
                ImGui::Render();
            };
            input.frame();
            input.frame();
            input.click("graph:selection");
            input.click("graph:select-all");
            check(canvas.selection == all, "Actual Select all differs");
            input.click("graph:selection");
            input.click("graph:select-invert");
            check(canvas.selection.empty(), "Actual Invert differs");
            input.click("graph:selection");
            input.click("graph:select-all");
            input.click("graph:selection");
            input.click("graph:select-clear");
            check(canvas.selection.empty() && document->revision() == revision &&
                      document->source().document == original,
                  "Transient selection changed source/history");
            const auto box = test::ui_targets.at("graph:canvas");
            auto start = ImVec2{box.minimum.x + 3, box.minimum.y + 3};
            auto end = ImVec2{box.maximum.x - 3, box.maximum.y - 3};
            auto rectangle = [&](ImGuiKey modifier = ImGuiKey_None) {
                if (modifier != ImGuiKey_None)
                    input.key(modifier, true);
                input.mouse(start, false);
                input.mouse(start, true);
                input.mouse(end, true);
                input.mouse(end, false);
                if (modifier != ImGuiKey_None)
                    input.key(modifier, false);
            };
            rectangle();
            check(canvas.selection == all, "Actual rectangle selection differs");
            rectangle(ImGuiMod_Alt);
            check(canvas.selection.empty(), "Actual rectangle remove differs");
            canvas.selection = {first};
            rectangle(ImGuiMod_Ctrl);
            check(canvas.selection == all, "Actual rectangle add differs");
            canvas.selection = {first};
            input.mouse(start, false);
            input.mouse(start, true);
            input.mouse(end, true);
            input.key(ImGuiKey_Escape, true);
            input.mouse(end, false);
            input.key(ImGuiKey_Escape, false);
            check(canvas.selection == std::set<GraphNodeId>{first},
                  "Escape lost selection baseline");
            input.mouse(start, true);
            input.mouse(end, true);
            locked = true;
            input.frame();
            input.mouse(end, false);
            check(canvas.selection == std::set<GraphNodeId>{first},
                  "Lock did not cancel rectangle");
            check(document->revision() == revision, "Rectangle changed history");
            locked = false;
            input.frame();
            input.mouse(start, true);
            input.mouse(end, true);
            document->edit(document->revision(), "Other source edit",
                           [](auto& s) { s["graph"]["nodes"][0]["position"] = {1, 2}; });
            input.frame();
            input.mouse(end, false);
            check(canvas.selection == std::set<GraphNodeId>{first},
                  "Source revision did not cancel rectangle");
            document->undo();
            input.frame();
            input.mouse(start, true);
            input.mouse(end, true);
            window_size.x -= 20;
            input.frame();
            input.mouse(end, false);
            check(canvas.selection == std::set<GraphNodeId>{first},
                  "Resize did not cancel rectangle");
            window_size.x += 20;
            input.frame();
            for (const auto key : {"left", "center-x", "right", "top", "center-y", "bottom",
                                   "space-x", "space-y", "grid"}) {
                canvas.selection = all;
                const auto before = document->source().document;
                const auto rev = document->revision();
                input.click("graph:arrange");
                input.click("graph:arrange:" + std::string(key));
                const auto after = document->source().document;
                check(material_graph_layout_equivalent(before["graph"], after["graph"]),
                      "Arrange lost source data");
                if (after != before) {
                    check(document->revision() == rev + 1, "Arrange is not one source edit");
                    document->undo();
                    canvas.reset();
                    input.frame();
                    check(document->source().document == before, "Arrange Undo differs");
                    document->redo();
                    canvas.reset();
                    input.frame();
                    check(document->source().document == after, "Arrange Redo differs");
                    document->undo();
                    canvas.reset();
                    input.frame();
                }
            }
            canvas.selection = all;
            locked = true;
            input.frame();
            check(!test::ui_targets.at("graph:arrange").enabled, "Readonly Arrange enabled");
            locked = false;
            input.frame();
            const auto header = input.point("graph:node:" + first.str());
            input.mouse(header, false);
            input.mouse(header, true);
            input.mouse({header.x + 30, header.y + 30}, true);
            locked = true;
            input.frame();
            input.mouse({header.x + 30, header.y + 30}, false);
            check(document->source().document == original, "Lock committed pending node drag");
            locked = false;
            canvas.reset();
            input.frame();
            const auto drag_header = input.point("graph:node:" + first.str());
            input.mouse(drag_header, false);
            input.mouse(drag_header, true);
            input.mouse({drag_header.x + 40, drag_header.y + 40}, true);
            input.key(ImGuiKey_Escape, true);
            input.mouse({drag_header.x + 40, drag_header.y + 40}, false);
            input.key(ImGuiKey_Escape, false);
            check(document->source().document == original, "Escape committed pending node drag");
            const auto scalar = original["graph"]["nodes"][2]["id"].get<GraphNodeId>();
            const auto surface = original["graph"]["nodes"][1]["id"].get<GraphNodeId>();
            const auto from = input.point("graph:output:" + scalar.str() + ":out");
            const auto to = input.point("graph:input:" + surface.str() + ":roughness");
            for (bool lock : {false, true}) {
                input.mouse(from, false);
                input.mouse(from, true);
                input.mouse(to, true);
                if (lock) {
                    locked = true;
                    input.frame();
                } else
                    input.key(ImGuiKey_Escape, true);
                input.mouse(to, false);
                locked = false;
                if (!lock)
                    input.key(ImGuiKey_Escape, false);
                input.frame();
                check(document->source().document == original, "Canceled port link changed source");
            }
            canvas.selection = all;
            input.click("graph:arrange");
            input.click("graph:arrange:grid");
            document->save();
            MaterialGraphDocument reopened(project.writer_guard(), document->locator());
            check(reopened.source().document == document->source().document,
                  "Layout reopen differs");
            document.reset();
            // The real editor reaches its compiler provider. This failure fixture
            // measures scheduling without substituting shader/GPU acceptance.
            unsigned requests = 0;
            MaterialGraphEditor editor({}, [&]() -> asset_detail::ShaderCompilerProfile {
                ++requests;
                throw std::runtime_error("Expected scheduling fixture compiler unavailable");
            });
            editor.open(project, "Assets/Layout.shader.json");
            std::string message;
            input.frame = [&] {
                editor.poll(project, message);
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({2300, 1800}, ImGuiCond_Always);
                editor.draw(project, false);
                ImGui::Render();
            };
            auto settle = [&] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                    input.frame();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                check(!editor.pending(), "Graph scheduling did not settle");
                input.frame();
            };
            settle();
            check(requests == 1, "Initial graph did not reach compiler provider");
            editor.canvas.selection = all;
            input.click("graph:arrange");
            input.click("graph:arrange:left");
            check(!editor.pending() && editor.dirty() && editor.can_undo(),
                  "Layout scheduled preview or lost dirty history");
            editor.undo();
            input.frame();
            editor.redo();
            input.frame();
            check(!editor.pending() && requests == 1, "Layout history scheduled compiler");
            const auto function_before = editor.document()->source().document;
            const auto function_id =
                function_before["graph"]["functions"][0]["id"].get<GraphFunctionId>();
            input.click("graph:functions");
            input.click("graph:function-edit:" + function_id.str());
            input.click("graph:selection");
            input.click("graph:select-all");
            input.click("graph:arrange");
            input.click("graph:arrange:grid");
            const auto function_after = editor.document()->source().document;
            check(function_after != function_before && !editor.pending() && requests == 1 &&
                      material_graph_layout_equivalent(function_before["graph"],
                                                       function_after["graph"]),
                  "Function body arrangement changed program or scheduled compiler");
            editor.undo();
            input.frame();
            check(editor.document()->source().document == function_before,
                  "Function layout Undo differs");
            editor.redo();
            input.frame();
            check(editor.document()->source().document == function_after && !editor.pending() &&
                      requests == 1,
                  "Function layout Redo changed scheduling/source");
            editor.canvas.selection = {first};
            input.frame();
            input.key(ImGuiMod_Ctrl, true);
            input.click("graph:node-value");
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            input.frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddInputCharactersUTF8("0.25");
            input.frame();
            input.key(ImGuiKey_Enter, true);
            input.key(ImGuiKey_Enter, false);
            settle();
            check(requests == 2,
                  "Value edit did not schedule compiler provider; requests=" +
                      std::to_string(requests) + " draft=" +
                      editor.canvas.preview_graph(editor.document()->source().document["graph"])
                          .dump());
            const auto scalar_id = original["graph"]["nodes"][2]["id"].get<GraphNodeId>();
            const auto surface_id = original["graph"]["nodes"][1]["id"].get<GraphNodeId>();
            const auto output = input.point("graph:output:" + scalar_id.str() + ":out");
            const auto roughness = input.point("graph:input:" + surface_id.str() + ":roughness");
            input.mouse(output, false);
            input.mouse(output, true);
            input.mouse(roughness, true);
            input.mouse(roughness, false);
            settle();
            check(requests == 3 &&
                      editor.document()->source().document["graph"]["edges"].size() == 2,
                  "Actual port connection did not schedule compiler");
            input.click("button:Refresh preview");
            settle();
            check(requests == 4, "Explicit Refresh did not reach compiler provider");
            unsigned publications = 0;
            editor.publish = [&](const auto&) {
                ++publications;
                return false;
            };
            editor.request_save();
            input.frame();
            check(!editor.document()->dirty() && publications == 1,
                  "Layout/value Save & Compile omitted source write/publication request");
            ImGui::DestroyContext();
        }
        std::cout << "Graph layout/projection, actual selection/history/cancel and compiler "
                     "scheduling100/150/200% passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
