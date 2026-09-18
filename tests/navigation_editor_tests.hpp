#pragma once
#include "component_inspector.hpp"
#include "navigation_tools.hpp"
void require(bool, const char*);
inline void test_navigation_editor() {
    const auto root =
        std::filesystem::current_path() / ("navigation-editor-" + forge::AssetId::generate().str());
    {
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        forge::SceneDocument project(scene);
        forge::SceneDocument::create_project(root, "Navigation test");
        project.open_project(root);
        auto id = forge::authoring_command(scene, "entity.create", {{"name", "Agent"}})
                      .at("selected")
                      .get<std::string>();
        forge::authoring_command(scene, "component.add",
                                 {{"entity", id}, {"component", "forge.navigation_agent"}});
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 900};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        forge::NavigationTools tools(root / "missing-worker");
        std::string message;
        ImGui::NewFrame();
        ImGui::SetNextWindowSize({1200, 900});
        ImGui::Begin("Navigation test");
        ImGui::SetNextItemOpen(true);
        forge::ComponentInspector inspector;
        inspector.draw(scene, project, id);
        require(message.empty(), "Navigation reflected fields failed");
        ImGui::SetNextItemOpen(true);
        tools.content(scene, project, false, message);
        tools.poll(scene, project, false, message);
        require(message.empty(), "Navigation content controls failed");
        auto* draw = ImGui::GetWindowDrawList();
        auto before = draw->VtxBuffer.Size;
        tools.draw(scene.effective_document(), {}, {600, 0}, {500, 800});
        require(draw->VtxBuffer.Size == before,
                "Disabled navigation overlay changed scene fixture");
        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext();
    }
    std::filesystem::remove_all(root);
}
