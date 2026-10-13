#include "editor/material_graph_canvas.hpp"
#include "material_graph_document.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need material geometry UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto lease = std::make_shared<ProjectLease>(root);
            auto document = MaterialGraphDocument::create(lease, "Assets/Geometry.shader.json");
            MaterialGraphCanvas canvas;
            MaterialGraphCanvas::Edit edit = [&](std::string label, const auto& op) {
                document->edit(document->revision(), std::move(label),
                               [&](auto& draft) { op(draft["graph"]); });
            };
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = {2400, 2000};
            io.DeltaTime = 1.f / 60;
            io.ConfigInputTrickleEventQueue = false;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::GetStyle().ScaleAllSizes(scale);
            io.FontGlobalScale = scale;
            ui::interface_scale = scale;
            ui::EditorUiContext context;
            ui::ContextScope scope(context);
            test::observe_ui = true;
            bool locked = false;
            auto frame = [&] {
                test::ui_targets.clear();
                ImGui::NewFrame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize({2300, 1800}, ImGuiCond_Always);
                ImGui::Begin("Material geometry");
                const auto graph = document->source().document.at("graph");
                canvas.draw(graph, document->revision(), edit, locked);
                ImGui::End();
                ImGui::Render();
            };
            auto point = [&](const std::string& id) {
                check(test::ui_targets.contains(id), "Missing material geometry UI " + id);
                const auto box = test::ui_targets.at(id);
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                check(box.enabled && p.x >= box.clip_minimum.x && p.x <= box.clip_maximum.x &&
                          p.y >= box.clip_minimum.y && p.y <= box.clip_maximum.y,
                      "Clipped/disabled material geometry UI " + id);
                return p;
            };
            auto mouse = [&](ImVec2 p, bool down) {
                io.AddMousePosEvent(p.x, p.y);
                io.AddMouseButtonEvent(0, down);
                frame();
            };
            auto click = [&](const std::string& id) {
                frame();
                const auto p = point(id);
                mouse(p, false);
                mouse(p, true);
                mouse(p, false);
                frame();
            };
            auto search = [&](const char* text) {
                click("graph:search");
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(text);
                frame();
            };
            frame();
            frame();
            for (const auto key : {"length", "distance", "reflect3", "refract3"}) {
                const auto before = document->source().document;
                click("graph:add-node");
                search(std::string_view(key) == "reflect3"   ? "Reflect"
                       : std::string_view(key) == "refract3" ? "Refract"
                                                             : key);
                click("graph:node-choice:" + std::string(key));
                const auto added = document->source().document;
                check(added["graph"]["nodes"].size() == before["graph"]["nodes"].size() + 1 &&
                          document->can_undo(),
                      "Geometry node creation lacks source history");
                const auto node = added["graph"]["nodes"].back();
                check(node["type"] == key && node["data"]["type"] == "vector3",
                      "Wrong created geometry schema/default type");
                const auto id = node["id"].get<GraphNodeId>();
                const bool metric =
                    std::string_view(key) == "length" || std::string_view(key) == "distance";
                check(material_graph_outputs(node)[0].type ==
                          (metric ? MaterialParameterType::Scalar : MaterialParameterType::Vector3),
                      "Actual created geometry node has wrong output type");
                if (!metric) {
                    frame();
                    check(!test::ui_targets.contains("graph:node-type"),
                          "Fixed 3D ray node offers an incompatible type control");
                    document->undo();
                    canvas.reset();
                    frame();
                    check(document->source().document == before, "Ray add is not one source Undo");
                    document->redo();
                    canvas.reset();
                    frame();
                    check(document->source().document == added, "Ray source Redo differs");
                    click("graph:node:" + id.str());
                    continue;
                }
                document->edit(document->revision(), "Opaque geometry metadata", [](auto& draft) {
                    draft["graph"]["nodes"].back()["data"]["opaque"] = {{"retain", 75}};
                });
                const auto with_opaque = document->source().document;
                frame();
                click("graph:node-type");
                click("graph:node-type:color3");
                canvas.flush(edit);
                frame();
                auto typed = document->source().document;
                check(typed["graph"]["nodes"].back()["data"]["type"] == "color3" &&
                          typed["graph"]["nodes"].back()["id"].get<GraphNodeId>() == id &&
                          typed["graph"]["nodes"].back()["data"]["opaque"]["retain"] == 75 &&
                          material_graph_outputs(typed["graph"]["nodes"].back())[0].type ==
                              MaterialParameterType::Scalar,
                      "Actual metric type control lost type/identity/opaque/scalar output");
                (void)compile_material_graph(typed["graph"]);
                locked = true;
                frame();
                check(!test::ui_targets.at("graph:node-type").enabled,
                      "Readonly metric type control remains writable");
                locked = false;
                frame();
                check(document->source().document == typed,
                      "Readonly metric drawing changed source");
                document->undo();
                canvas.reset();
                frame();
                check(document->source().document == with_opaque,
                      "Geometry type change was not one source Undo");
                document->redo();
                canvas.reset();
                frame();
                check(document->source().document == typed, "Geometry type Redo differs");
                document->undo();
                document->undo();
                document->undo();
                canvas.reset();
                frame();
                check(document->source().document == before,
                      "Geometry add Undo did not restore source");
                document->redo();
                document->redo();
                document->redo();
                canvas.reset();
                frame();
                check(document->source().document == typed, "Geometry node/source Redo differs");
                click("graph:node:" + id.str());
            }
            const auto final_source = document->source().document;
            locked = true;
            frame();
            check(!test::ui_targets.at("graph:add-node").enabled,
                  "Readonly geometry controls remain writable");
            locked = false;
            frame();
            check(document->source().document == final_source,
                  "Readonly drawing changed geometry source");
            document->save();
            MaterialGraphDocument reopened(lease, "Assets/Geometry.shader.json");
            check(reopened.source().document == final_source,
                  "Geometry source save/reopen differs");
            (void)compile_material_graph(reopened.source().document["graph"]);
            ImGui::DestroyContext();
        }
        std::cout << "Actual material vector geometry "
                     "discovery/type/history/readonly/save100/150/200% passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
