#include "../src/physics_joint_value.hpp"
#include "component_inspector.hpp"
#include <forge/authoring.hpp>
#include <forge/scene_identity.hpp>
#include <imgui_internal.h>
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
        require(argc == 2, "Need joint Inspector scratch root");
        const auto scratch = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(scratch);
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = scratch / AssetId::generate().str();
            SceneDocument::create_project(root, "Joint Inspector test");
            {
                EngineContext engine;
                Scene scene{engine.world()};
                scene.reset(
                    {{"version", 1},
                     {"entities",
                      Json::array(
                          {{{"id", "a"}, {"name", "Anchor"}, {"components", Json::object()}},
                           {{"id", "b"}, {"name", "Bob"}, {"components", Json::object()}},
                           {{"id", "j"}, {"name", "Joint"}, {"components", Json::object()}}})}});
                scene.entity("a").set<PhysicsBody>({});
                PhysicsBody moving;
                moving.motion = 2;
                scene.entity("b").set<PhysicsBody>(moving);
                const auto staged = scene.snapshot();
                SceneDocument project(scene);
                project.open_project(root);
                scene.restore_snapshot(staged);
                ComponentInspector inspector;
                const auto id = scene.canonical_id("j");
                authoring_command(scene, "component.add",
                                  {{"entity", id}, {"component", "forge.physics_joint"}});
                auto schema = scene.schema();
                Json joint_schema;
                for (const auto& c : schema.at("components"))
                    if (c.at("id") == "forge.physics_joint")
                        joint_schema = c;
                require(!joint_schema.is_null(), "Joint absent from Inspector schema");
                const EntityRef a{scene.asset_id(), EntityId::parse(scene.canonical_id("a"))};
                const EntityRef b{scene.asset_id(), EntityId::parse(scene.canonical_id("b"))};
                ImGui::CreateContext();
                auto& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.DisplaySize = {1500, 1000};
                io.DeltaTime = 1.f / 60;
                io.FontGlobalScale = scale;
                io.ConfigInputTrickleEventQueue = false;
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                unsigned char* pixels;
                int width, height;
                io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
                ui::interface_scale = scale;
                ui::EditorUiContext context;
                context.scene = &scene;
                ui::ContextScope scope(context);
                test::observe_ui = true;
                std::string selected = id;
                auto frame = [&] {
                    test::ui_targets.clear();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({1500, 1000}, ImGuiCond_Always);
                    ImGui::Begin("Joint Inspector");
                    inspector.draw(scene, project, selected);
                    ImGui::End();
                    ImGui::Render();
                };
                auto target = [&](const std::string& name) {
                    frame();
                    require(test::ui_targets.contains(name), "Missing widget: " + name);
                    auto t = test::ui_targets.at(name);
                    if (t.minimum.y < t.clip_minimum.y || t.maximum.y > t.clip_maximum.y) {
                        auto* window = ImGui::FindWindowByName("Joint Inspector");
                        require(window, "Joint Inspector window missing");
                        ImGui::SetScrollY(window,
                                          window->Scroll.y + (t.minimum.y + t.maximum.y -
                                                              t.clip_minimum.y - t.clip_maximum.y) *
                                                                 .5f);
                        frame();
                        frame();
                        t = test::ui_targets.at(name);
                    }
                    require(t.enabled && t.minimum.y >= t.clip_minimum.y &&
                                t.maximum.y <= t.clip_maximum.y,
                            "Clipped widget: " + name);
                    return ImVec2{(t.minimum.x + t.maximum.x) / 2, (t.minimum.y + t.maximum.y) / 2};
                };
                auto click = [&](const std::string& name) {
                    auto at = target(name);
                    io.AddMousePosEvent(at.x, at.y);
                    frame();
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                };
                auto key = [&](ImGuiKey k) {
                    io.AddKeyEvent(k, true);
                    frame();
                    io.AddKeyEvent(k, false);
                    frame();
                };
                auto numeric = [&](const char* field, const char* text,
                                   const char* component = "forge.physics_joint") {
                    click(std::string("component-field:") + component + ":" + field);
                    io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    key(ImGuiKey_A);
                    io.AddKeyEvent(ImGuiMod_Ctrl, false);
                    frame();
                    io.AddInputCharactersUTF8(text);
                    frame();
                    key(ImGuiKey_Enter);
                };
                frame();
                frame();
                frame();
                require(
                    !test::ui_targets.contains("component-field:forge.physics_joint:axis_a") &&
                        !test::ui_targets.contains("component-field:forge.physics_joint:min_angle"),
                    "Distance Inspector exposes inactive orientation/angle controls");
                click("component-field:forge.physics_joint:body_a");
                require(!test::ui_targets.contains("entity-picker-row:" + id),
                        "Joint picker offered a non-body entity");
                click("entity-picker-row:" + a.entity.str());
                require(scene.entity(id).get<PhysicsJoint>().body_a == a,
                        "Actual Body A picker failed");
                click("component-field:forge.physics_joint:body_b");
                click("entity-picker-row:" + b.entity.str());
                require(scene.entity(id).get<PhysicsJoint>().body_b == b,
                        "Actual Body B picker failed");
                numeric("max_distance", "3");
                numeric("min_distance", "2");
                require(scene.entity(id).get<PhysicsJoint>().min_distance == 2 &&
                            scene.entity(id).get<PhysicsJoint>().max_distance == 3,
                        "Inspector distance edit");
                require(scene.undo() && scene.entity(id).get<PhysicsJoint>().min_distance == 1,
                        "Joint Inspector undo");
                require(scene.redo() && scene.entity(id).get<PhysicsJoint>().min_distance == 2,
                        "Joint Inspector redo");
                click("component-field:forge.physics_joint:kind");
                key(ImGuiKey_DownArrow);
                key(ImGuiKey_Enter);
                require(scene.entity(id).get<PhysicsJoint>().kind == 1,
                        "Actual Hinge enum choice failed");
                frame();
                require(!test::ui_targets.contains(
                            "component-field:forge.physics_joint:min_distance") &&
                            test::ui_targets.contains("component-field:forge.physics_joint:axis_a"),
                        "Hinge Inspector exposes irrelevant distance fields or lacks axes");
                numeric("min_angle", "-30");
                numeric("max_angle", "60");
                require(scene.entity(id).get<PhysicsJoint>().min_angle == -30 &&
                            scene.entity(id).get<PhysicsJoint>().max_angle == 60,
                        "Inspector angle edit");
                require(test::ui_targets.contains(
                            "component-field:forge.physics_joint:hinge_motor_speed") &&
                            !test::ui_targets.contains(
                                "component-field:forge.physics_joint:slider_motor_speed"),
                        "Hinge drive fields lost their kind-specific visibility");
                click("component-field:forge.physics_joint:motor_enabled");
                require(scene.entity(id).get<PhysicsJoint>().motor_enabled,
                        "Actual motor checkbox failed");
                require(scene.undo() && !scene.entity(id).get<PhysicsJoint>().motor_enabled &&
                            scene.redo() && scene.entity(id).get<PhysicsJoint>().motor_enabled,
                        "Motor checkbox Undo/Redo failed");
                numeric("hinge_motor_speed", "-45");
                numeric("max_motor_torque", "20");
                require(scene.entity(id).get<PhysicsJoint>().hinge_motor_speed == -45 &&
                            scene.entity(id).get<PhysicsJoint>().max_motor_torque == 20,
                        "Actual signed hinge speed/torque fields failed");
                click("component-field:forge.physics_joint:body_b");
                click("entity-picker-clear");
                require(!scene.entity(id).get<PhysicsJoint>().body_b.scene,
                        "Reference clear failed");
                require(scene.undo() && scene.entity(id).get<PhysicsJoint>().body_b == b,
                        "Reference clear undo");
                for (const unsigned expected : {2u, 3u, 4u}) {
                    click("component-field:forge.physics_joint:kind");
                    key(ImGuiKey_DownArrow);
                    key(ImGuiKey_Enter);
                    require(scene.entity(id).get<PhysicsJoint>().kind == expected,
                            "Actual Point/Fixed enum choice failed");
                    frame();
                    require(!test::ui_targets.contains(
                                "component-field:forge.physics_joint:min_distance") &&
                                !test::ui_targets.contains(
                                    "component-field:forge.physics_joint:min_angle") &&
                                test::ui_targets.contains(
                                    "component-field:forge.physics_joint:body_a") &&
                                (test::ui_targets.contains(
                                     "component-field:forge.physics_joint:axis_a") ==
                                 (expected >= 3)) &&
                                (test::ui_targets.contains(
                                     "component-field:forge.physics_joint:min_travel") ==
                                 (expected == 4)) &&
                                (test::ui_targets.contains(
                                     "component-field:forge.physics_joint:motor_enabled") ==
                                 (expected == 4)),
                            "Actual Point/Fixed Inspector fields differ");
                    require(scene.undo() &&
                                scene.entity(id).get<PhysicsJoint>().kind == expected - 1,
                            "Point/Fixed selection Undo failed");
                    require(scene.redo() && scene.entity(id).get<PhysicsJoint>().kind == expected,
                            "Point/Fixed selection Redo failed");
                }
                numeric("min_travel", "-2");
                numeric("max_travel", "3");
                require(scene.entity(id).get<PhysicsJoint>().min_travel == -2 &&
                            scene.entity(id).get<PhysicsJoint>().max_travel == 3,
                        "Actual slider travel controls failed");
                require(scene.undo() && scene.entity(id).get<PhysicsJoint>().max_travel == 1,
                        "Slider travel Undo failed");
                require(scene.redo() && scene.entity(id).get<PhysicsJoint>().max_travel == 3,
                        "Slider travel Redo failed");
                numeric("slider_motor_speed", "-2");
                numeric("max_motor_force", "3");
                require(scene.entity(id).get<PhysicsJoint>().slider_motor_speed == -2 &&
                            scene.entity(id).get<PhysicsJoint>().max_motor_force == 3 &&
                            scene.entity(id).get<PhysicsJoint>().hinge_motor_speed == -45 &&
                            scene.entity(id).get<PhysicsJoint>().max_motor_torque == 20 &&
                            scene.entity(id).get<PhysicsJoint>().motor_enabled,
                        "Slider effort/target or inactive hinge settings changed");
                require(
                    scene.undo() && scene.entity(id).get<PhysicsJoint>().max_motor_force == 100 &&
                        scene.redo() && scene.entity(id).get<PhysicsJoint>().max_motor_force == 3,
                    "Motor numeric Undo/Redo failed");
                click("component-field:forge.physics_joint:enabled");
                require(!scene.entity(id).get<PhysicsJoint>().enabled, "Enabled checkbox failed");
                const auto retained = scene.entity(id).get<PhysicsJoint>();
                require(retained.min_distance == 2 && retained.max_distance == 3 &&
                            retained.min_angle == -30 && retained.max_angle == 60,
                        "Switching joint kind discarded inactive authored settings");
                scene.save(root / "joint.scene.json");
                EngineContext reopened;
                Scene saved{reopened.world()};
                saved.load(root / "joint.scene.json");
                require(saved.entity(id).get<PhysicsJoint>() ==
                            scene.entity(id).get<PhysicsJoint>(),
                        "Inspector save/reopen lost settings");
                // The shared enum focus correction also serves existing body fields.
                selected = scene.canonical_id("b");
                click("component-field:forge.physics_body:motion");
                key(ImGuiKey_UpArrow);
                key(ImGuiKey_Enter);
                require(scene.entity(selected).get<PhysicsBody>().motion == 1,
                        "Existing Dynamic body enum failed to navigate to Kinematic");
                require(scene.undo() && scene.entity(selected).get<PhysicsBody>().motion == 2,
                        "Existing body enum history regressed");
                click("component-field:forge.physics_body:continuous_collision");
                require(scene.entity(selected).get<PhysicsBody>().continuous_collision,
                        "Actual continuous-collision checkbox failed");
                require(scene.undo() &&
                            !scene.entity(selected).get<PhysicsBody>().continuous_collision,
                        "Continuous-collision checkbox Undo failed");
                require(scene.redo() &&
                            scene.entity(selected).get<PhysicsBody>().continuous_collision,
                        "Continuous-collision checkbox Redo failed");
                click("component-field:forge.physics_body:motion");
                key(ImGuiKey_UpArrow);
                key(ImGuiKey_Enter);
                require(scene.entity(selected).get<PhysicsBody>().motion == 1 &&
                            scene.entity(selected).get<PhysicsBody>().continuous_collision,
                        "Changing body mode discarded its stored CCD choice");
                for (const auto& [field, text] : {std::pair{"linear_damping", "2"},
                                                  {"angular_damping", "3"},
                                                  {"max_linear_velocity", "4"},
                                                  {"max_angular_velocity", "90"}}) {
                    const auto before = scene.entity(selected).get<PhysicsBody>();
                    numeric(field, text, "forge.physics_body");
                    const auto changed = scene.entity(selected).get<PhysicsBody>();
                    require(changed != before, "Actual body tuning widget did not commit");
                    require(scene.undo() && scene.entity(selected).get<PhysicsBody>() == before,
                            "Body tuning widget Undo failed");
                    require(scene.redo() && scene.entity(selected).get<PhysicsBody>() == changed,
                            "Body tuning widget Redo failed");
                }
                click("component-field:forge.physics_body:allow_sleeping");
                const auto tuning = scene.entity(selected).get<PhysicsBody>();
                require(tuning.linear_damping == 2 && tuning.angular_damping == 3 &&
                            tuning.max_linear_velocity == 4 && tuning.max_angular_velocity == 90 &&
                            !tuning.allow_sleeping && tuning.motion == 1,
                        "Inspector tuning lost numeric values, sleep policy or inactive mode");
                require(scene.undo() && scene.entity(selected).get<PhysicsBody>().allow_sleeping,
                        "Sleeping checkbox Undo failed");
                require(scene.redo() && !scene.entity(selected).get<PhysicsBody>().allow_sleeping,
                        "Sleeping checkbox Redo failed");
                scene.save(root / "joint.scene.json");
                saved.load(root / "joint.scene.json");
                require(saved.entity(selected).get<PhysicsBody>() == tuning,
                        "Save/reopen lost inactive body tuning or continuous-collision choices");
                bool solver_absent = false;
                try {
                    (void)engine.services().physics();
                } catch (const std::exception&) {
                    solver_absent = true;
                }
                require(solver_absent, "Authoring Inspector created a solver");
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
            require(std::filesystem::remove_all(root) > 0, "Joint UI cleanup failed");
        }
        ui::interface_scale = 1;
        std::cout << "Actual scaled joint body picker, enum, numbers, history, clear, checkbox and "
                     "persistence passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
