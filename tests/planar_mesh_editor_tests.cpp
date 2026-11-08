#include "editor/editable_mesh_editor.hpp"
#include "imgui_input_driver.hpp"
#include "imgui_software_capture.hpp"
#include <iostream>

using namespace forge;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        const bool single_scale = argc == 3 && std::string_view(argv[2]) == "--single-scale";
        const bool selected_scale = argc == 4 && std::string_view(argv[2]) == "--scale";
        float only_scale = 1;
        if (selected_scale) {
            const auto value = std::string_view(argv[3]);
            require(value == "1" || value == "1.5" || value == "2", "Choose UI scale 1, 1.5 or 2");
            only_scale = value == "2" ? 2 : value == "1.5" ? 1.5f : 1;
        }
        require(argc == 2 || single_scale || selected_scale,
                "Need planar Mesh UI scratch and optional --single-scale or --scale 1/1.5/2");
        const auto scratch = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(scratch);
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        struct Quit {
            ~Quit() { SDL_Quit(); }
        } quit;
        for (float scale : {1.f, 1.5f, 2.f}) {
            if ((single_scale || selected_scale) && scale != only_scale)
                continue;
            const auto root = scratch / AssetId::generate().str();
            SceneDocument::create_project(root, "Planar Mesh controls");
            std::filesystem::create_directories(root / "Assets");
            EngineContext engine;
            Scene scene(engine.world());
            SceneDocument project(scene);
            project.open_project(root);
            const auto authored = scene.snapshot();
            test::ImGuiSoftwareCapture capture(int(700 * scale), int(800 * scale));
            ui::style(scale);
            ImGui::GetIO().ConfigInputTrickleEventQueue = false;
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ui::EditorUiContext owner;
            owner.scene = &scene;
            ui::ContextScope context(owner);
            test::observe_ui = true;
            auto editor = std::make_unique<EditableMeshEditor>();
            bool locked = false;
            auto frame = [&] {
                test::ui_targets.clear();
                capture.new_frame();
                ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
                ImGui::Begin("Content", nullptr, ImGuiWindowFlags_NoResize);
                editor->content(project, locked);
                ImGui::End();
                capture.render();
            };
            test::ImGuiInputDriver input(frame);
            auto key = [&](ImGuiKey value) {
                ImGui::GetIO().AddKeyEvent(value, true);
                frame();
                ImGui::GetIO().AddKeyEvent(value, false);
                frame();
            };
            auto shape = [&](unsigned index) {
                input.click("editable-mesh:create-shape");
                key(ImGuiKey_Home);
                for (unsigned i = 0; i < index; ++i)
                    key(ImGuiKey_DownArrow);
                key(ImGuiKey_Enter);
                frame();
            };
            frame();
            input.click("editable-mesh:new");
            require(editor->has_unapplied_edits(),
                    "Creation intent was absent from source draft checks");
            shape(2);
            input.type("editable-mesh:create-path", "Assets/Quad.mesh.json");
            input.type("editable-mesh:create-width", "4");
            input.type("editable-mesh:create-height", "2");
            locked = true;
            frame();
            require(!test::ui_targets.at("editable-mesh:create").enabled,
                    "Busy creation remained enabled");
            locked = false;
            input.click("editable-mesh:create");
            require(editor->document() && !editor->has_unapplied_edits() &&
                        scene.snapshot() == authored,
                    "Quad creation changed the Scene or lost its source document");
            const auto quad = editor->document()->source().cook().lods[0].parts[0];
            require(quad.bounds.minimum == std::array<float, 3>{-2, -1, 0} &&
                        quad.bounds.maximum == std::array<float, 3>{2, 1, 0},
                    "Quad controls lost dimensions");
            editor = std::make_unique<EditableMeshEditor>();
            input.click("editable-mesh:new");
            shape(3);
            input.type("editable-mesh:create-path", "Assets/Polygon.mesh.json");
            input.click("editable-mesh:create-add");
            input.click("editable-mesh:create");
            require(!editor->document() && editor->has_unapplied_edits() &&
                        !std::filesystem::exists(root / "Assets/Polygon.mesh.json"),
                    "Invalid outline wrote a source or discarded its draft");
            input.type("editable-mesh:create-x:4", "-1");
            if (scale == 1)
                capture.save(scratch / "planar-mesh-create.png");
            input.click("editable-mesh:create");
            require(editor->document() &&
                        editor->document()->source().document.at("vertices").size() == 5 &&
                        scene.snapshot() == authored,
                    "Polygon controls did not create five ordinary Mesh vertices");
            editor = std::make_unique<EditableMeshEditor>();
            input.click("editable-mesh:new");
            shape(4);
            input.type("editable-mesh:create-path", "Assets/Concave.mesh.json");
            input.click("editable-mesh:create-add");
            input.click("editable-mesh:create-add");
            const std::array<std::array<const char*, 2>, 6> boundary{{{{"0", "0"}},
                                                                      {{"3", "0"}},
                                                                      {{"3", "1"}},
                                                                      {{"1", "1"}},
                                                                      {{"1", "3"}},
                                                                      {{"0", "3"}}}};
            for (unsigned i = 0; i < boundary.size(); ++i) {
                input.type(("editable-mesh:create-x:" + std::to_string(i)).c_str(), boundary[i][0]);
                input.type(("editable-mesh:create-y:" + std::to_string(i)).c_str(), boundary[i][1]);
            }
            if (scale == 1)
                capture.save(scratch / "concave-mesh-create.png");
            input.click("editable-mesh:create");
            require(editor->document() &&
                        editor->document()->source().document.at("vertices").size() == 6 &&
                        editor->document()->source().document.at("faces").size() == 4 &&
                        scene.snapshot() == authored,
                    "Concave controls did not create ordinary triangle faces without Scene edits");
            editor = std::make_unique<EditableMeshEditor>();
            input.click("editable-mesh:new");
            shape(5);
            input.type("editable-mesh:create-path", "Assets/Frame.mesh.json");
            input.click("editable-mesh:create-loop");
            key(ImGuiKey_Home);
            key(ImGuiKey_DownArrow);
            key(ImGuiKey_Enter);
            frame();
            input.type("editable-mesh:create-hole:0:x:0", "-0.5");
            input.click("editable-mesh:create");
            require(!editor->document() && editor->has_unapplied_edits() &&
                        !std::filesystem::exists(root / "Assets/Frame.mesh.json"),
                    "Touching hole wrote a source or discarded its creation draft");
            input.type("editable-mesh:create-hole:0:x:0", "-0.25");
            input.click("editable-mesh:create-hole-add");
            input.click("editable-mesh:create-hole-add");
            frame();
            require(!test::ui_targets.at("editable-mesh:create-hole-add").enabled,
                    "Native hole draft exceeded three interior loops");
            input.click("editable-mesh:create-hole-remove");
            input.click("editable-mesh:create-hole-remove");
            input.click("editable-mesh:create-hole:0:add");
            input.click("editable-mesh:create-hole:0:remove:4");
            frame();
            if (scale == 1)
                capture.save(scratch / "mesh-holes-create.png");
            input.click("editable-mesh:create");
            require(editor->document() &&
                        editor->document()->source().document.at("vertices").size() == 8 &&
                        editor->document()->source().document.at("faces").size() == 8 &&
                        scene.snapshot() == authored,
                    "Native hole controls did not create an ordinary editable frame Mesh");
            editor = std::make_unique<EditableMeshEditor>();
            input.click("editable-mesh:new");
            shape(3);
            for (unsigned i = 4; i < 16; ++i)
                input.click("editable-mesh:create-add");
            frame();
            require(!test::ui_targets.at("editable-mesh:create-add").enabled,
                    "Outline exceeded sixteen corners");
            input.click("editable-mesh:create-cancel");
            require(!editor->has_unapplied_edits() && !editor->document(),
                    "Cancel retained creation authority");
            input.click("editable-mesh:new");
            project.open_project(root);
            const auto reopened = scene.snapshot();
            frame();
            require(!test::ui_targets.at("editable-mesh:create").enabled,
                    "Retired project generation admitted creation");
            input.click("editable-mesh:create-cancel");
            require(scene.snapshot() == reopened, "Cancel mutated the reopened Scene");
            test::observe_ui = false;
            std::cout << "Planar/concave/hole creation controls at scale " << scale << " passed\n"
                      << std::flush;
        }
        std::cout << "Native planar Mesh creation, convex rejection, scrolling, scope and "
                     "cancellation passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
