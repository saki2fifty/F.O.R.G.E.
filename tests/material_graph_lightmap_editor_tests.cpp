#include "editor/material_graph_canvas.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include "material_graph_document.hpp"
#include "material_lightmap_fixture.hpp"
#include <iostream>
using namespace forge;
namespace {
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need lightmap UI evidence directory");
        const auto evidence = std::filesystem::absolute(argv[1]);
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = evidence / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto lease = std::make_shared<ProjectLease>(root);
            auto document = MaterialGraphDocument::create(lease, "Assets/Lightmap.shader.json");
            document->edit(document->revision(), "Imported lightmap fixture", [](auto& value) {
                value["graph"] = lightmap_test::graph();
                value["graph"]["edges"].erase(value["graph"]["edges"].end() - 1);
                value["graph"]["nodes"][2]["data"]["semantic"] = "color";
            });
            const auto graph = document->source().document.at("graph");
            const auto texture = graph["nodes"][2]["id"].get<GraphNodeId>();
            const auto surface = graph["nodes"][1]["id"].get<GraphNodeId>();
            const auto multiply = graph["nodes"].back()["id"].get<GraphNodeId>();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2400, 2000};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::GetStyle().ScaleAllSizes(scale);
            io.FontGlobalScale = scale;
            ui::interface_scale = scale;
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            MaterialGraphCanvas canvas;
            canvas.selection = {texture};
            bool locked = false;
            MaterialGraphCanvas::Edit edit = [&](std::string label, const auto& operation) {
                document->edit(document->revision(), std::move(label),
                               [&](auto& value) { operation(value["graph"]); });
            };
            auto frame = [&] {
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({2300, 1800}, ImGuiCond_Always);
                ImGui::Begin("Imported lightmap graph");
                const auto current = document->source().document.at("graph");
                canvas.draw(current, document->revision(), edit, locked);
                ImGui::End();
                ImGui::Render();
            };
            test::ImGuiInputDriver input(frame);
            frame();
            frame();
            input.click("graph:node-usage");
            frame();
            const auto option = test::ui_targets.at("graph:node-usage:hdr");
            const ImVec2 option_center{(option.minimum.x + option.maximum.x) / 2,
                                       (option.minimum.y + option.maximum.y) / 2};
            check(option.enabled && option_center.x >= option.clip_minimum.x &&
                      option_center.x <= option.clip_maximum.x &&
                      option_center.y >= option.clip_minimum.y &&
                      option_center.y <= option.clip_maximum.y,
                  "HDR menu choice cannot be reached");
            io.AddMousePosEvent(option_center.x, option_center.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            canvas.flush(edit);
            const auto hdr = document->source().document;
            check(hdr["graph"]["nodes"][2]["data"]["semantic"] == "hdr",
                  "Actual Texture Usage did not author HDR selection");
            document->undo();
            canvas.reset();
            check(document->source().document.at("graph") == graph,
                  "HDR selection is not one source Undo");
            document->redo();
            canvas.reset();
            frame();
            auto point = [&](const std::string& id) {
                const auto box = test::ui_targets.at(id);
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                check(box.enabled && p.x >= box.clip_minimum.x && p.x <= box.clip_maximum.x &&
                          p.y >= box.clip_minimum.y && p.y <= box.clip_maximum.y,
                      "Lightmap port clipped or disabled");
                return p;
            };
            const auto from = point("graph:output:" + multiply.str() + ":out");
            const auto to = point("graph:input:" + surface.str() + ":lightmap_irradiance");
            io.AddMousePosEvent(from.x, from.y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMousePosEvent(to.x, to.y);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            canvas.flush(edit);
            const auto connected = document->source().document;
            check(connected["graph"]["edges"].size() == hdr["graph"]["edges"].size() + 1 &&
                      connected["graph"]["edges"].back()["to"]["port"] == "lightmap_irradiance",
                  "Actual output drag did not connect lightmap irradiance");
            check(compile_material_graph(connected["graph"])
                          .surface.textures.at("lightmap")
                          .semantic == TextureSemantic::HdrColor,
                  "Native graph lost its HDR binding");
            document->undo();
            canvas.reset();
            check(document->source().document == hdr, "Lightmap wire is not one source Undo");
            document->redo();
            canvas.reset();
            canvas.selection = {texture};
            locked = true;
            frame();
            check(!test::ui_targets.at("graph:node-usage").enabled &&
                      !test::ui_targets.at("graph:node-uv").enabled,
                  "Readonly lightmap Texture controls remain writable");
            check(document->source().document == connected, "Readonly drawing changed lightmap");
            locked = false;
            document->save();
            MaterialGraphDocument reopened(lease, "Assets/Lightmap.shader.json");
            check(reopened.source().document == connected, "Lightmap source save/reopen differs");
            ImGui::DestroyContext();
        }
        {
            test::ImGuiSoftwareCapture capture(1360, 880);
            ui::style(1);
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            MaterialGraphCanvas canvas;
            auto value = lightmap_test::graph();
            canvas.selection = {value["nodes"][2]["id"].get<GraphNodeId>()};
            for (unsigned i = 0; i < 4; ++i) {
                capture.new_frame();
                ImGui::SetNextWindowPos({8, 8}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({1344, 864}, ImGuiCond_Always);
                ImGui::Begin("Imported lightmap graph");
                canvas.draw(
                    value, 1, [&](std::string, const auto& operation) { operation(value); }, false);
                ImGui::End();
                capture.render();
            }
            capture.save(evidence / "imported-lightmap-graph.png");
        }
        std::cout << "Lightmap native Texture usage/port wire/history/readonly/save at100/150/200% "
                     "passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
