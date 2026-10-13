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
        check(argc == 2, "Need material math UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            auto lease = std::make_shared<ProjectLease>(root);
            auto document = MaterialGraphDocument::create(lease, "Assets/Math.shader.json");
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
                ImGui::Begin("Material math");
                const auto graph = document->source().document.at("graph");
                canvas.draw(graph, document->revision(), edit, locked);
                ImGui::End();
                ImGui::Render();
            };
            auto point = [&](const std::string& id) {
                check(test::ui_targets.contains(id), "Missing material math UI " + id);
                const auto box = test::ui_targets.at(id);
                const ImVec2 p{(box.minimum.x + box.maximum.x) / 2,
                               (box.minimum.y + box.maximum.y) / 2};
                check(box.enabled && p.x >= box.clip_minimum.x && p.x <= box.clip_maximum.x &&
                          p.y >= box.clip_minimum.y && p.y <= box.clip_maximum.y,
                      "Clipped/disabled material math UI " + id);
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
            for (const auto key : {"step", "smoothstep", "ceiling", "round"}) {
                const auto before = document->source().document;
                click("graph:add-node");
                search(key);
                click("graph:node-choice:" + std::string(key));
                const auto added = document->source().document;
                check(added["graph"]["nodes"].size() == before["graph"]["nodes"].size() + 1 &&
                          document->can_undo(),
                      "Math node creation lacks source history");
                const auto node = added["graph"]["nodes"].back();
                check(node["type"] == key && node["data"]["type"] == "scalar",
                      "Wrong created math schema/default type");
                const auto id = node["id"].get<GraphNodeId>();
                click("graph:node-type");
                click("graph:node-type:color3");
                canvas.flush(edit);
                frame();
                auto typed = document->source().document;
                check(typed["graph"]["nodes"].back()["data"]["type"] == "color3" &&
                          typed["graph"]["nodes"].back()["id"].get<GraphNodeId>() == id,
                      "Actual math type control changed identity or lost type");
                (void)compile_material_graph(typed["graph"]);
                document->undo();
                canvas.reset();
                frame();
                check(document->source().document == added,
                      "Math type change was not one source Undo");
                document->redo();
                canvas.reset();
                frame();
                check(document->source().document == typed, "Math type Redo differs");
                document->undo();
                document->undo();
                canvas.reset();
                frame();
                check(document->source().document == before,
                      "Math add Undo did not restore source");
                document->redo();
                document->redo();
                canvas.reset();
                frame();
                check(document->source().document == typed, "Math node/source Redo differs");
                click("graph:node:" + id.str());
            }
            const auto final_source = document->source().document;
            locked = true;
            frame();
            check(!test::ui_targets.at("graph:add-node").enabled &&
                      !test::ui_targets.at("graph:node-type").enabled,
                  "Readonly math controls remain writable");
            locked = false;
            frame();
            check(document->source().document == final_source,
                  "Readonly drawing changed math source");
            document->save();
            MaterialGraphDocument reopened(lease, "Assets/Math.shader.json");
            check(reopened.source().document == final_source, "Math source save/reopen differs");
            (void)compile_material_graph(reopened.source().document["graph"]);
            ImGui::DestroyContext();
        }
        std::cout << "Actual material threshold/rounding "
                     "discovery/type/history/readonly/save100/150/200% passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
