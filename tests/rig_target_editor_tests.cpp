#include "editor/component_inspector.hpp"
#include "editor/scene_tools.hpp"
#include <forge/prefab_authoring.hpp>
#include <iostream>
using namespace forge;
namespace {
const char* stage = "model axis math";
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double a, double b, double tolerance = 1e-4) {
    if (!std::isfinite(a) || std::abs(a - b) >= tolerance)
        throw std::runtime_error(std::string(stage) + ": " + std::to_string(a) + " differs from " +
                                 std::to_string(b));
}
std::string create(Scene& scene, const char* name) {
    return authoring_command(scene, "entity.create", {{"name", name}}).at("selected");
}
double target_x(Scene& scene, const std::string& id) {
    return scene.entity(id).get<TwoBoneIK>().target_x;
}
} // namespace
int main() {
    try {
        EngineContext engine;
        Scene scene(engine.world());
        const auto parent = create(scene, "Parent"), actor = create(scene, "Rig");
        scene.entity(parent).set(LocalTranslation{2, 3, 4}).set(rotation_from_euler({25, 35, 15}));
        scene.reparent_entity(actor, parent, ReparentMode::KeepLocal);
        scene.entity(actor)
            .set(ModelSource{{AssetId::generate()}, {}})
            .set(TwoBoneIK{})
            .set(AimIK{});
        const auto snapshot = scene.snapshot();
        RigTargetGesture gesture;
        for (unsigned signs = 0; signs < 8; ++signs) {
            scene.entity(parent).set(
                LocalScale{signs & 1 ? -2.f : 2.f, signs & 2 ? -3.f : 3.f, signs & 4 ? -4.f : 4.f});
            scene.entity(actor).set(rotation_from_euler({15, 20, 45}));
            const auto target =
                rig_target_view(scene.effective_document(), actor, "forge.two_bone_ik");
            check(bool(target), "Placed rig target missing");
            EditorCamera camera;
            const auto world = target->world();
            camera.target = {float(world[0]), float(world[1]), float(world[2])};
            camera.yaw = .35f;
            camera.pitch = -.25f;
            camera.distance = 12;
            for (unsigned axis = 0; axis < 3; ++axis) {
                const auto basis = target->axis(axis);
                auto next = world;
                for (unsigned i = 0; i < 3; ++i)
                    next[i] += basis[i] * .4;
                const auto first = project_debug_point(camera, world, {1000, 800});
                const auto last = project_debug_point(camera, next, {1000, 800});
                check(bool(first) && bool(last), "Rig drag projection missing");
                const auto delta = rig_axis_drag(camera, world, basis, *first, *last, 1000, 800);
                check(bool(delta), "Parented mirrored/nonuniform rig axis unavailable");
                near(*delta, .4, 3e-4);
            }
        }
        scene.restore_snapshot(snapshot);
        stage = "snap/history";
        check(gesture.begin(scene, actor, "forge.two_bone_ik", 0), "Rig gesture did not begin");
        const auto before = scene.snapshot();
        check(gesture.update(.45, true, .25), "Rig snap rejected");
        near(gesture.target().target[0], 1.5);
        check(scene.snapshot() == before, "Rig draft changed authored scene");
        check(gesture.commit(scene), "Rig target did not commit");
        near(target_x(scene, actor), 1.5);
        check(scene.undo() && scene.snapshot() == before,
              "One Undo did not restore the entire target drag");
        check(scene.redo(), "Rig target Redo failed");
        near(target_x(scene, actor), 1.5);
        scene.undo();
        check(gesture.begin(scene, actor, "forge.two_bone_ik", 0) && gesture.update(.3, false, 1),
              "Rig cancel draft failed");
        gesture.cancel();
        check(scene.snapshot() == before, "Rig cancellation changed source");
        const auto neutral_revision = scene.revision();
        check(gesture.begin(scene, actor, "forge.two_bone_ik", 0) && gesture.update(0, false, 1) &&
                  !gesture.commit(scene) && scene.revision() == neutral_revision,
              "A neutral target click added scene history");
        check(gesture.begin(scene, actor, "forge.two_bone_ik", 0), "Revision fixture failed");
        scene.rename_entity(actor, "Changed");
        bool rejected = false;
        try {
            gesture.commit(scene);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && !gesture.active(), "Rig drag crossed a scene revision change");
        scene.entity(actor).set(LocalScale{0, 1, 1});
        check(!gesture.begin(scene, actor, "forge.two_bone_ik", 0) &&
                  gesture.begin(scene, actor, "forge.two_bone_ik", 1),
              "Singular axis substituted identity or blocked an independent usable axis");
        gesture.cancel();
        scene.restore_snapshot(snapshot);
        auto malformed = scene.effective_document();
        for (auto& row : malformed.at("entities"))
            if (row.at("id") == actor)
                row["world_affine"][0] = "unavailable";
        check(!rig_target_view(malformed, actor, "forge.two_bone_ik"),
              "Malformed observed affine became a target marker");
        check(!rig_target_view(scene.effective_document(), actor, "forge.local_translation"),
              "Unrelated component became a rig target");
        const auto scratch = std::filesystem::temp_directory_path() /
                             ("forge-rig-target-" + AssetId::generate().str());
        SceneDocument::create_project(scratch, "Rig viewport test");
        {
            SceneDocument project(scene);
            project.open_project(scratch);
            scene.restore_snapshot(snapshot);
            for (const std::string type : {"forge.two_bone_ik", "forge.aim_ik"})
                for (float scale : {1.f, 1.5f, 2.f}) {
                    stage = "actual mouse drag";
                    ImGui::CreateContext();
                    auto& io = ImGui::GetIO();
                    io.IniFilename = nullptr;
                    io.DisplaySize = {1100, 900};
                    io.DeltaTime = 1.f / 60;
                    io.ConfigInputTrickleEventQueue = false;
                    unsigned char* pixels;
                    int width, height;
                    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                    ui::style(scale);
                    ui::SceneTools tools;
                    const unsigned axis = type == "forge.two_bone_ik" ? 0 : 2;
                    ComponentInspector inspector;
                    inspector.fixture_focus_component = type;
                    unsigned activations = 0;
                    inspector.edit_rig_target = [&](const auto& component) {
                        check(component == type, "Inspector selected the wrong target tool");
                        ++activations;
                        tools.target_tool(component);
                    };
                    EditorCamera camera;
                    camera.yaw = .35f;
                    camera.pitch = -.25f;
                    camera.distance = 10;
                    const auto focus =
                        rig_target_view(scene.effective_document(), actor, type)->world();
                    camera.target = {float(focus[0]), float(focus[1]), float(focus[2])};
                    auto selected = actor;
                    std::string status;
                    unsigned picks = 0;
                    bool inspect = true, can_edit = true, marker_checked = false;
                    ImVec2 viewport_origin{10, 40}, viewport_size{1000, 800};
                    auto frame = [&] {
                        test::ui_targets.clear();
                        test::observe_ui = true;
                        ImGui::NewFrame();
                        ImGui::SetNextWindowPos({0, 0});
                        ImGui::SetNextWindowSize({1100, 900});
                        ImGui::Begin(inspect ? "Inspector" : "Scene");
                        if (inspect) {
                            inspector.draw(scene, project, selected);
                            ImGui::End();
                            ImGui::Render();
                            return;
                        }
                        ImGui::SetCursorScreenPos(viewport_origin);
                        ImGui::InvisibleButton("Viewport", viewport_size,
                                               ImGuiButtonFlags_MouseButtonLeft);
                        const ui::ViewportInput input{ImGui::IsItemHovered(), ImGui::IsItemActive(),
                                                      ImGui::IsItemActivated()};
                        tools.input(scene, camera, selected, viewport_origin, viewport_size, input,
                                    can_edit, status, [&](const auto&, float, float) {
                                        ++picks;
                                        return std::string{};
                                    });
                        tools.draw(scene.effective_document(), camera, selected, {10, 40},
                                   {1000, 800}, true);
                        const bool marker = tools.draw_rig(scene.effective_document(), camera,
                                                           selected, {10, 40}, {1000, 800});
                        if (!marker_checked) {
                            check(marker, "Scene target marker was not projected");
                            const auto eye = camera.eye(), right = camera.right(), up = camera.up(),
                                       forward = camera.forward();
                            const AffineTransform placement{{right[0], up[0], forward[0], eye[0],
                                                             right[1], up[1], forward[1], eye[1],
                                                             right[2], up[2], forward[2], eye[2]}};
                            const auto view = camera_view(Camera{}, placement, 1000, 800);
                            const GameDebugView game{view, 1000, 800};
                            const auto authored = scene.snapshot();
                            auto observed = scene.effective_document();
                            for (auto& row : observed.at("entities"))
                                if (row.at("id") == actor)
                                    row["components"][type]["target_x"] = 1.25;
                            check(tools.draw_rig(observed, game, selected, {10, 40}, {1000, 800},
                                                 true),
                                  "Observed Live target was not projected through the game camera");
                            auto* draw = ImGui::GetWindowDrawList();
                            draw->PushClipRect({10, 40}, {110, 140}, true);
                            check(!tools.draw_rig(observed, game, selected, {10, 40}, {1000, 800},
                                                  true),
                                  "Target marker crossed a different camera viewport's clip");
                            draw->PopClipRect();
                            check(scene.snapshot() == authored,
                                  "Drawing Live targets changed authored state");
                            marker_checked = true;
                        }
                        ImGui::End();
                        ImGui::Render();
                    };
                    frame();
                    frame();
                    frame();
                    const auto button = test::ui_targets.at("rig-target-tool:" + type);
                    check(button.enabled && button.minimum.y >= button.clip_minimum.y &&
                              button.maximum.y <= button.clip_maximum.y,
                          "Inspector target tool clipped or disabled");
                    const auto inspector_source = scene.snapshot();
                    io.AddMousePosEvent((button.minimum.x + button.maximum.x) / 2,
                                        (button.minimum.y + button.maximum.y) / 2);
                    frame();
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                    check(activations == 1 && tools.rig_component == type &&
                              scene.snapshot() == inspector_source,
                          "Inspector target tool did not select the existing viewport owner");
                    inspect = false;
                    frame();
                    frame();
                    const auto original = scene.snapshot();
                    const auto target = *rig_target_view(scene.effective_document(), actor, type);
                    const auto world = target.world(), basis = target.axis(axis);
                    const auto eye = camera.eye(), forward = camera.forward();
                    double depth = 0, length = 0;
                    for (unsigned i = 0; i < 3; ++i) {
                        depth += (world[i] - eye[i]) * forward[i];
                        length += basis[i] * basis[i];
                    }
                    const auto display = 75 * scale * 2 * depth / (EditorCamera::focal * 800);
                    auto press = world;
                    for (unsigned i = 0; i < 3; ++i)
                        press[i] += basis[i] * .7 * display / std::sqrt(length);
                    auto next = press;
                    for (unsigned i = 0; i < 3; ++i)
                        next[i] += basis[i] * .4;
                    const auto start = *project_debug_point(camera, press, {1000, 800});
                    const auto end = *project_debug_point(camera, next, {1000, 800});
                    // ImGui floors incoming cursor positions to physical pixels. The
                    // analytic projection test above proves subpixel model-axis math;
                    // actual mouse input is bounded by the two rounded endpoints.
                    const auto projected_center = *project_debug_point(camera, world, {1000, 800});
                    auto unit_endpoint = world;
                    for (unsigned i = 0; i < 3; ++i)
                        unit_endpoint[i] += basis[i];
                    const auto projected_unit =
                        *project_debug_point(camera, unit_endpoint, {1000, 800});
                    const double cursor_tolerance =
                        3 / std::hypot(projected_unit[0] - projected_center[0],
                                       projected_unit[1] - projected_center[1]);
                    io.AddMousePosEvent(10 + start[0], 40 + start[1]);
                    frame();
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    check(tools.rig.active() && tools.rig.axis() == axis && !tools.move.active() &&
                              picks == 0,
                          "Rig axes lost exclusive viewport ownership");
                    io.AddMousePosEvent(10 + end[0], 40 + end[1]);
                    frame();
                    check(scene.snapshot() == original, "Actual target mouse draft changed source");
                    near(tools.rig.target().target[axis], target.target[axis] + .4,
                         cursor_tolerance);
                    const auto draft = scene.preview_document(tools.preview(scene.document()));
                    near(rig_target_view(draft, actor, type)->target[axis],
                         tools.rig.target().target[axis]);
                    check(!tools.selection_matches({actor, parent}),
                          "Active rig target allowed an unrelated selection");
                    io.AddMouseButtonEvent(0, false);
                    frame();
                    check(!tools.active(), "Rig mouse release retained gesture capture");
                    near(rig_target_view(scene.effective_document(), actor, type)->target[axis],
                         target.target[axis] + .4, cursor_tolerance);
                    check(scene.undo() && scene.snapshot() == original,
                          "Mouse target drag did not use one Undo entry");
                    if (scale == 1.f) {
                        for (unsigned reason = 0; reason < 7; ++reason) {
                            io.AddMousePosEvent(10 + start[0], 40 + start[1]);
                            frame();
                            io.AddMouseButtonEvent(0, true);
                            frame();
                            check(tools.rig.active(),
                                  "Cancellation drag did not capture the target");
                            io.AddMousePosEvent(10 + end[0], 40 + end[1]);
                            frame();
                            check(scene.snapshot() == original,
                                  "Cancellation draft changed source");
                            if (reason == 0)
                                io.AddKeyEvent(ImGuiKey_Escape, true);
                            if (reason == 1)
                                selected = parent;
                            if (reason == 2)
                                can_edit =
                                    false; // Play, busy or focus loss at the consumer boundary.
                            if (reason == 3)
                                viewport_size.x += 5;
                            if (reason == 4)
                                viewport_origin.x += 5;
                            if (reason == 5)
                                io.AddMouseButtonEvent(1, true);
                            if (reason == 6)
                                scene.rename_entity(actor, "External revision");
                            const auto expected = scene.snapshot();
                            frame();
                            check(!tools.active() && scene.snapshot() == expected,
                                  "Rig cancellation committed a partial target draft");
                            io.AddKeyEvent(ImGuiKey_Escape, false);
                            io.AddMouseButtonEvent(0, false);
                            io.AddMouseButtonEvent(1, false);
                            selected = actor;
                            can_edit = true;
                            viewport_origin = {10, 40};
                            viewport_size = {1000, 800};
                            frame();
                            if (reason == 6)
                                check(scene.undo() && scene.snapshot() == original,
                                      "Cancellation added history after the external revision");
                        }
                        io.AddKeyEvent(ImGuiKey_Q, true);
                        frame();
                        io.AddKeyEvent(ImGuiKey_Q, false);
                        frame();
                        check(tools.rig_component.empty() && !tools.move_tool,
                              "Q did not restore object selection from the rig target tool");
                        io.AddKeyEvent(ImGuiKey_W, true);
                        frame();
                        io.AddKeyEvent(ImGuiKey_W, false);
                        frame();
                        check(tools.rig_component.empty() && tools.move_tool &&
                                  scene.snapshot() == original,
                              "W did not restore object Move without changing rig data");
                    }
                    ImGui::DestroyContext();
                }
        }
        std::filesystem::remove_all(scratch);
        const auto prefab = create_prefab_source(scene, actor);
        stage = "prefab target";
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto instance = instantiate_prefab(scene, prefab.asset());
        check(!scene.entity(instance).owns<TwoBoneIK>(), "Rig prefab did not begin inherited");
        const auto inherited = scene.snapshot();
        check(gesture.begin(scene, instance, "forge.two_bone_ik", 0) &&
                  gesture.update(.25, false, 1) && gesture.commit(scene),
              "Inherited rig target could not be manipulated");
        near(target_x(scene, instance), 1.25);
        check(scene.prefab_sources().at(prefab.asset()) == prefab.source,
              "Rig target manipulation rewrote the prefab source");
        check(scene.undo() && scene.snapshot() == inherited &&
                  !scene.entity(instance).owns<TwoBoneIK>(),
              "Rig target Undo did not restore prefab inheritance intent");
        std::cout << "Rig model-axis math, source-free drag preview, actual scaled mouse ownership "
                     "and history passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
