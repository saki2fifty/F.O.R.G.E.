#pragma once
#include "orientation.hpp"
#include "transform_gesture.hpp"
#include "workspace.hpp"
void require(bool condition, const char* message);
inline void test_transforms() {
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    const auto id = forge::create_primitive(scene, 0, {1, 2, 3});
    forge::authoring_command(scene, "transform.rotation",
                             {{"entity", id}, {"value", {{"x", 23}, {"y", 41}, {"z", 17}}}});
    const auto original = scene.document();
    const forge::ObjectTransform before(scene.effective_document().at("entities").at(0));
    forge::TransformGesture g;
    for (int axis = 0; axis < 3; ++axis) {
        require(g.begin(scene, id, forge::TransformGesture::Mode::Rotate, {0, 0, 1}),
                "Cannot begin rotation");
        g.constrain(axis);
        require(g.update(90), "Cannot rotate world axis");
        const auto preview = scene.preview_document(g.preview(original));
        const forge::ObjectTransform after(preview.at("entities").at(0));
        forge::Float3 a{};
        a[axis] = 1;
        for (unsigned i = 0; i < 3; ++i) {
            const auto cross = forge::geom_cross(a, before.axes[i]);
            for (unsigned j = 0; j < 3; ++j)
                require(std::abs(after.axes[i][j] -
                                 (cross[j] + a[j] * forge::geom_dot(a, before.axes[i]))) < .00002f,
                        "World rotation composition failed on rotated object");
        }
        require(scene.document() == original, "Preview mutated authored state");
        g.cancel();
        require(scene.document() == original, "Cancel changed scene");
    }
    require(g.begin(scene, id, forge::TransformGesture::Mode::Scale, {0, 0, 1}), "Cannot scale");
    g.constrain(1);
    require(g.update(2), "Local scale failed");
    require(g.value() == forge::Float3{1, 2, 1}, "Constrained scale changed other axes");
    require(!g.update(0) && !g.update(-1) && !g.update(10001), "Unsupported scale accepted");
    g.constrain(2);
    require(g.update(3) && g.value() == forge::Float3{1, 1, 3},
            "Axis switch accumulated old preview");
    require(g.accept(scene), "Scale commit failed");
    forge::authoring_history(scene, false);
    require(scene.document() == original, "Transform must undo in one step");
    g.begin(scene, id, forge::TransformGesture::Mode::Rotate, {0, 0, 1});
    g.update(45);
    forge::authoring_command(scene, "entity.rename", {{"entity", id}, {"name", "Changed"}});
    const auto changed = scene.document();
    bool rejected = false;
    try {
        g.accept(scene);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && scene.document() == changed && !g.active(),
            "Stale transform was committed");
    g.begin(scene, id, forge::TransformGesture::Mode::Rotate, {0, 0, 1});
    require(g.update(0) && !g.accept(scene) && scene.document() == changed,
            "Neutral rotation made an edit");
    forge::EditorCamera camera;
    for (unsigned axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1}) {
            const auto target = camera.target;
            camera.align(axis, sign);
            const auto f = camera.forward(), r = camera.right(), u = camera.up();
            require(std::abs(f[axis] + sign) < .000001f && camera.target == target,
                    "Axis alignment changed pivot or wrong view");
            require(std::abs(forge::geom_dot(r, u)) < .000001f &&
                        std::abs(forge::geom_dot(f, u)) < .000001f &&
                        std::abs(forge::geom_dot(u, u) - 1) < .000001f,
                    "Pole basis singularity");
            camera.orbit(0, sign > 0 ? -1 : 1);
            require(std::isfinite(camera.eye()[1]), "Orbit from pole broke camera");
        }
    char old_id[16], new_id[16];
    std::snprintf(old_id, sizeof(old_id), "0x%08X", ImHashStr("World"));
    std::snprintf(new_id, sizeof(new_id), "0x%08X", ImHashStr("Hierarchy###World"));
    const std::string layout =
        std::string("[Window][World]\nPos=70,80\nDockId=0x00000002\n[Docking][Data]\nSelected=") +
        old_id;
    const auto migrated = forge::ui::migrate_layout(layout);
    require(migrated.find("Pos=70,80") != std::string::npos &&
                migrated.find("[Window][Hierarchy###World]") != std::string::npos &&
                migrated.find(new_id) != std::string::npos,
            "Custom layout migration lost geometry/reference");
    require(forge::ui::migrate_layout(migrated) == migrated, "Layout migration is not idempotent");
}
inline void test_interaction_input(float scale) {
    ImGui::CreateContext();
    forge::ui::style(scale);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1400, 900};
    io.DeltaTime = 1.0f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    forge::EngineContext scene_engine;
    forge::Scene scene(scene_engine.world());
    const auto id = forge::create_primitive(scene, 0, {0, 1, 0});
    const auto original = scene.document();
    forge::ui::ModalTransform modal;
    forge::ui::OrientationGizmo gizmo;
    forge::EditorCamera camera;
    std::string status;
    bool allowed = true;
    bool navigation_activated = false;
    ImVec2 origin{}, size{700, 500};
    auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({1000, 800});
        ImGui::Begin("Scene interaction");
        origin = ImGui::GetCursorScreenPos();
        const bool blocked = gizmo.input(camera, origin, size, allowed && !modal.active());
        ImGui::SetCursorScreenPos(origin);
        forge::ui::ViewportInput input;
        forge::ui::camera_controls(camera, size, allowed, &input, blocked || modal.active());
        navigation_activated = input.activated;
        modal.input(scene, id, camera, origin, size, !blocked && ImGui::IsWindowHovered(), allowed,
                    status);
        gizmo.draw(camera, origin, size);
        modal.draw(origin, size);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    io.AddMousePosEvent(200, 200);
    frame();
    auto key = [&](ImGuiKey k) {
        io.AddKeyEvent(k, true);
        frame();
        io.AddKeyEvent(k, false);
        frame();
    };
    key(ImGuiKey_S);
    require(modal.active(), "S did not start scale");
    key(ImGuiKey_X);
    key(ImGuiKey_2);
    key(ImGuiKey_Enter);
    require(!modal.active(), "Enter did not confirm");
    auto doc = scene.document();
    require(doc["entities"][0]["components"]["forge.local_scale"]["x"] == 2,
            "Numeric constrained scale failed");
    forge::authoring_history(scene, false);
    require(scene.document() == original, "Numeric transform did not undo once");
    key(ImGuiKey_R);
    key(ImGuiKey_Z);
    key(ImGuiKey_9);
    key(ImGuiKey_0);
    key(ImGuiKey_Escape);
    require(!modal.active() && scene.document() == original,
            "Escape failed to cancel typed rotation");
    key(ImGuiKey_R);
    allowed = false;
    frame();
    require(!modal.active(), "Focus/availability loss did not cancel");
    allowed = true;
    io.AddMouseButtonEvent(1, true);
    frame();
    key(ImGuiKey_S);
    require(!modal.active(), "RMB+S started scale instead of flight");
    io.AddMouseButtonEvent(1, false);
    frame();
    camera.yaw = .5f;
    camera.pitch = .3f;
    frame();
    const auto point = gizmo.ends[2];
    io.AddMousePosEvent(point.x, point.y);
    frame();
    io.AddMouseButtonEvent(0, true);
    frame();
    require(!navigation_activated, "Orientation press leaked to scene selection");
    io.AddMouseButtonEvent(0, false);
    frame();
    require(std::abs(camera.pitch + forge::EditorCamera::pole) < .00001f,
            "Orientation Y endpoint failed");
    require(scene.document() == original, "Camera gizmo edited scene");
    forge::ui::Workspace workspace;
    workspace.scene = false;
    forge::ui::Workspace restored;
    restored.load({{"panels", workspace.settings()}});
    require(!restored.scene && restored.hierarchy, "Panel visibility persistence failed");
    ImGui::DestroyContext();
}
