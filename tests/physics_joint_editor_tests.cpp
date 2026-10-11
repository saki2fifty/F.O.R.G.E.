#include "../src/physics_joint_value.hpp"
#include "property_drawer.hpp"
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
        for (float scale : {.65f, 1.f, 2.f}) {
            const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
            std::filesystem::create_directories(root);
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
                bool hinge = false;
                auto frame = [&] {
                    test::ui_targets.clear();
                    ImGui::NewFrame();
                    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
                    ImGui::SetNextWindowSize({1500, 1000}, ImGuiCond_Always);
                    ImGui::Begin("Joint Inspector");
                    const auto current = scene.entity(id).get<PhysicsJoint>();
                    auto values = physics_detail::joint_value(current);
                    for (const auto& field : joint_schema.at("fields")) {
                        const std::string name = field.at("id");
                        if (name != "body_a" && name != "body_b" && name != "kind" &&
                            name != "enabled" &&
                            (hinge ? name != "min_angle" && name != "max_angle"
                                   : name != "min_distance" && name != "max_distance"))
                            continue;
                        ui::IdScope field_scope(name.c_str());
                        auto value = values.at(name);
                        if (property_field(root, field, value))
                            authoring_command(scene, "property.set",
                                              {{"entity", id},
                                               {"component", "forge.physics_joint"},
                                               {"field", name},
                                               {"value", value}});
                        test::observe_item("joint-property:" + name);
                    }
                    ImGui::End();
                    ImGui::Render();
                };
                auto target = [&](const std::string& name) {
                    frame();
                    require(test::ui_targets.contains(name), "Missing widget: " + name);
                    const auto t = test::ui_targets.at(name);
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
                auto numeric = [&](const char* field, const char* text) {
                    click(std::string("joint-property:") + field);
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
                click("joint-property:body_a");
                require(!test::ui_targets.contains("entity-picker-row:" + id),
                        "Joint picker offered a non-body entity");
                click("entity-picker-row:" + a.entity.str());
                require(scene.entity(id).get<PhysicsJoint>().body_a == a,
                        "Actual Body A picker failed");
                click("joint-property:body_b");
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
                click("joint-property:kind");
                key(ImGuiKey_DownArrow);
                key(ImGuiKey_Enter);
                require(scene.entity(id).get<PhysicsJoint>().kind == 1,
                        "Actual Hinge enum choice failed");
                hinge = true;
                numeric("min_angle", "-30");
                numeric("max_angle", "60");
                require(scene.entity(id).get<PhysicsJoint>().min_angle == -30 &&
                            scene.entity(id).get<PhysicsJoint>().max_angle == 60,
                        "Inspector angle edit");
                click("joint-property:body_b");
                click("entity-picker-clear");
                require(!scene.entity(id).get<PhysicsJoint>().body_b.scene,
                        "Reference clear failed");
                require(scene.undo() && scene.entity(id).get<PhysicsJoint>().body_b == b,
                        "Reference clear undo");
                click("joint-property:enabled");
                require(!scene.entity(id).get<PhysicsJoint>().enabled, "Enabled checkbox failed");
                scene.save(root / "joint.scene.json");
                EngineContext reopened;
                Scene saved{reopened.world()};
                saved.load(root / "joint.scene.json");
                require(saved.entity(id).get<PhysicsJoint>() ==
                            scene.entity(id).get<PhysicsJoint>(),
                        "Inspector save/reopen lost settings");
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
