#pragma once
#include "animation_debug.hpp"
#include "animation_tools.hpp"
#include "component_inspector.hpp"
void require(bool, const char*);
inline void test_animation_editor() {
    const auto root =
        std::filesystem::current_path() / ("animation-editor-" + forge::AssetId::generate().str());
    {
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        forge::SceneDocument project(scene);
        forge::SceneDocument::create_project(root, "Animation test");
        project.open_project(root);
        auto id = forge::authoring_command(scene, "entity.create", {{"name", "Actor"}})
                      .at("selected")
                      .get<std::string>();
        forge::authoring_command(scene, "component.add",
                                 {{"entity", id}, {"component", "forge.animator"}});
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1000, 700};
        io.DeltaTime = 1.f / 60;
        unsigned char* pixels;
        int w, h;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        forge::AnimationTools tools(root / "missing-converter");
        std::string message;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1000, 700});
        ImGui::Begin("Animation test");
        ImGui::SetNextItemOpen(true);
        forge::ComponentInspector inspector;
        inspector.draw(scene, project, id);
        require(message.empty(), "Animator Inspector failed to draw reflected fields");
        ImGui::SetNextItemOpen(true);
        tools.content(project, false, message);
        tools.poll(project, message);
        require(message.empty(), "Animation content controls failed");
        scene.entity(id).set<forge::LocalTranslation>({2, 1, 0});
        scene.entity(id).set<forge::LocalRotation>(forge::rotation_from_euler({0, 0, 90}));
        auto doc = scene.effective_document();
        std::array<float, 16> a{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, b = a;
        b[13] = 2;
        doc["entities"][0]["animation_pose"] = {{"parents", {-1, 0}}, {"model", {a, b}}};
        auto* draw = ImGui::GetWindowDrawList();
        const int before = draw->VtxBuffer.Size;
        forge::EditorCamera camera;
        forge::draw_animation_debug(doc, camera, {400, 100}, {550, 500});
        require(draw->VtxBuffer.Size > before, "Debug skeleton overlay produced no geometry");
        const auto center = forge::project_point(camera, {0, 1, 0}, 550, 500);
        require(bool(center), "Debug joint unexpectedly outside camera");
        bool joint_near_expected = false;
        for (int i = before; i < draw->VtxBuffer.Size; ++i) {
            auto p = draw->VtxBuffer[i].pos;
            if (std::hypot(p.x - 400 - (*center)[0], p.y - 100 - (*center)[1]) < 5)
                joint_near_expected = true;
        }
        require(joint_near_expected, "Debug skeleton ignored existing camera projection");
        doc["entities"][0].erase("animation_pose");
        const int without = draw->VtxBuffer.Size;
        forge::draw_animation_debug(doc, camera, {400, 100}, {550, 500});
        require(draw->VtxBuffer.Size == without, "Nonanimated scene acquired overlay geometry");
        ImGui::End();
        ImGui::Render();
        ImGui::DestroyContext();
    }
    std::filesystem::remove_all(root);
}
