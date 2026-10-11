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
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need shading UI scratch root");
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root / "Assets");
            const auto source = EditableMeshSource::create_cube(AssetId::generate());
            atomic_write(root / "Assets/Shading.mesh.json", source.document.dump(2));
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
            editor.open(project, "Assets/Shading.mesh.json");
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
                require(test::ui_targets.contains(name), "Missing shading target");
                auto box = test::ui_targets.at(name);
                if (box.minimum.y < box.clip_minimum.y || box.maximum.y > box.clip_maximum.y) {
                    auto* w = ImGui::FindWindowByName("###Editable Mesh");
                    require(w, "Mesh window missing");
                    ImGui::SetScrollY(w, w->Scroll.y + (box.minimum.y + box.maximum.y -
                                                        box.clip_minimum.y - box.clip_maximum.y) /
                                                           2);
                    frame();
                    frame();
                    box = test::ui_targets.at(name);
                }
                require(box.enabled && box.minimum.y >= box.clip_minimum.y &&
                            box.maximum.y <= box.clip_maximum.y,
                        "Disabled/clipped shading target");
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
            frame();
            frame();
            const auto baseline = editor.document()->source().document;
            frame();
            require(!test::ui_targets.at("button:Shade flat").enabled,
                    "Current flat action must be disabled");
            click("button:Shade smooth");
            const auto shaded = editor.document()->source().document;
            auto oracle = source;
            oracle.set_smooth_shading(true);
            require(shaded == oracle.document && editor.document()->can_undo(),
                    "Actual shading action differs from source oracle/history");
            frame();
            require(!test::ui_targets.at("button:Shade smooth").enabled,
                    "Current smooth action must be disabled");
            click("button:Undo");
            require(editor.document()->source().document == baseline, "Shade Undo differs");
            click("button:Redo");
            require(editor.document()->source().document == shaded, "Shade Redo differs");
            locked = true;
            frame();
            require(!test::ui_targets.at("button:Shade flat").enabled &&
                        !test::ui_targets.at("button:Shade smooth").enabled,
                    "Locked shading enabled");
            locked = false;
            frame();
            click("button:Shade flat");
            const auto all = editor.document()->source().document;
            oracle.set_smooth_shading(false);
            require(all == oracle.document, "Flat source action differs");
            click("button:Undo");
            click("button:Undo");
            require(editor.document()->source().document == baseline,
                    "Two shading Undo steps failed");
            click("button:Shade smooth");
            require(!editor.document()->can_redo(), "Shading branch retained discarded redo");
            const auto published = editor.document()->source().document;
            click("button:Save & Publish");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (editor.pending() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                frame();
            }
            require(!editor.pending() && !editor.dirty(), "Shading publication failed");
            editor.request_close();
            frame();
            require(!editor.is_open(), "Published shading document did not close");
            editor.open(project, "Assets/Shading.mesh.json");
            frame();
            require(editor.document()->source().document == published,
                    "Shading save/reopen lost values");
            require(scene.document() == scene_before,
                    "Shading action changed Scene ownership/history");
            editor.request_close();
            frame();
            auto opposite = source;
            opposite.document["asset_id"] = AssetId::generate();
            opposite.document["vertices"] =
                nlohmann::json::array({{{"id", 1}, {"position", {0, 0, 0}}},
                                       {{"id", 2}, {"position", {1, 0, 0}}},
                                       {{"id", 3}, {"position", {0, 1, 0}}}});
            const auto corner = [](unsigned v) {
                return nlohmann::json{{"vertex", v}, {"uv", {0, 0}}};
            };
            opposite.document["faces"] = nlohmann::json::array(
                {{{"id", 4}, {"corners", {corner(1), corner(2), corner(3)}}},
                 {{"id", 5}, {"corners", {corner(1), corner(3), corner(2)}}}});
            opposite.document["next_id"] = 6;
            opposite.validate();
            atomic_write(root / "Assets/Opposite.mesh.json", opposite.document.dump(2));
            editor.open(project, "Assets/Opposite.mesh.json");
            frame();
            require(bool(editor.document()), "Opposite-normal fixture failed to open");
            const auto rejected = editor.document()->source().document;
            const auto revision = editor.document()->revision();
            click("button:Shade smooth");
            require(editor.document()->source().document == rejected &&
                        editor.document()->revision() == revision && !editor.document()->can_undo(),
                    "Rejected smooth normals changed source/history");
            require(scene.document() == scene_before, "Rejected shading changed Scene ownership");
            editor.request_close();
            frame();
            test::observe_ui = false;
            ImGui::DestroyContext();
            std::filesystem::remove_all(root);
        }
        ui::interface_scale = 1;
        std::cout
            << "Actual flat/smooth/current/lock/history/branch/publish/reopen/rejection65/100/200% "
               "passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
