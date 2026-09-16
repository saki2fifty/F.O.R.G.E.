#pragma once
#include "blockout.hpp"
#include "camera.hpp"
void require(bool condition, const char* message);
inline void test_blockout() {
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    std::vector<std::string> ids;
    for (unsigned kind = 0; kind < 4; ++kind)
        ids.push_back(forge::create_primitive(scene, kind, {float(kind) * 3, 1, 0}));
    require(scene.entity_count() == 4, "Palette creation failed");
    auto doc = scene.document();
    auto& first = forge::blockout_entity(doc, ids[0]);
    first["components"]["forge.rotation"] = {{"x", 20}, {"y", 35}, {"z", 12}};
    first["components"]["forge.scale"] = {{"x", 2}, {"y", 3}, {"z", 4}};
    scene.edit(doc);
    forge::BlockoutProperties properties;
    properties.copy_transform(scene, ids[0]);
    const auto before = scene.document();
    properties.paste_transform(scene, ids[1]);
    const auto pasted = scene.document();
    require(forge::blockout_entity(doc, ids[0])["components"]["forge.rotation"] ==
                pasted["entities"][1]["components"]["forge.rotation"],
            "Transform clipboard rotation mismatch");
    require(pasted["entities"][1]["components"]["forge.primitive"]["kind"] == 1,
            "Paste changed shape");
    require(scene.undo() && scene.document() == before, "Transform paste is not undoable");
    forge::EditorCamera camera;
    require(camera.frame(scene.document(), "", 1.0f), "Scaled primitive framing failed");
    const auto eye = camera.eye(), forward = camera.forward(), right = camera.right(),
               up = camera.up();
    const auto authored = scene.document();
    for (const auto& e : authored.at("entities")) {
        const forge::ObjectTransform transform(e);
        for (const auto& vertex : forge::primitive_meshes()[forge::primitive_kind(e)]) {
            const auto delta = forge::geom_sub(transform.point(vertex.position), eye);
            const auto depth = forge::geom_dot(delta, forward);
            require(depth > 0 &&
                        std::abs(forge::geom_dot(delta, right) * camera.focal / depth) <= 1 &&
                        std::abs(forge::geom_dot(delta, up) * camera.focal / depth) <= 1,
                    "Fit scene clipped transformed mesh");
        }
    }
}
inline void test_property_drag(float scale, const char* component = "forge.rotation") {
    ImGui::CreateContext();
    forge::ui::style(scale);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1100, 650};
    io.DeltaTime = 1.0f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    const auto id = forge::create_primitive(scene, 0, {0, 1, 0});
    scene.reset(scene.document());
    const auto before = scene.document();
    forge::BlockoutProperties properties;
    ImVec2 begin, end;
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({20, 20});
        ImGui::SetNextWindowSize({900, 500});
        ImGui::Begin("Property test");
        ImGui::SetNextItemWidth(600);
        properties.vector_control(scene, id, component);
        begin = ImGui::GetItemRectMin();
        end = ImGui::GetItemRectMax();
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    const ImVec2 press{begin.x + 40, (begin.y + end.y) / 2};
    io.AddMousePosEvent(press.x, press.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMousePosEvent(press.x + 60, press.y);
    frame();
    require(properties.active() && scene.document() == before,
            "Inspector drag did not stage outside authored scene");
    require(properties.preview(before)["entities"][0]["components"][component]["x"] !=
                before["entities"][0]["components"][component]["x"],
            "Inspector preview did not rotate");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    require(!properties.active() && scene.document() != before, "Inspector release did not commit");
    require(scene.undo() && scene.document() == before && !scene.undo(),
            "Inspector drag not one undo command");
    io.AddMousePosEvent(press.x, press.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMousePosEvent(press.x + 50, press.y);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    require(!properties.active() && scene.document() == before,
            "Escape changed authored transform");
    io.AddKeyEvent(ImGuiKey_Escape, false);
    io.AddMousePosEvent(press.x + 80, press.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    require(!properties.active() && scene.document() == before,
            "Cancelled drag restarted before release");
    ImGui::DestroyContext();
}
