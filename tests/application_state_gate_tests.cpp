#include "builtins.hpp"
#include <forge/application_state_activation.hpp>
#include <forge/application_states.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/scene.hpp>
#include <future>
#include <iostream>

namespace {
using namespace forge;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid State Condition was accepted");
}
ApplicationStatesDefinition definition() {
    return {{{"app",
              "",
              "menu",
              {{"menu", "", ""}, {"game", "", ""}, {"playing", "", "game"}, {"paused", "", "game"}},
              false,
              {},
              {}},
             {"hud",
              "",
              "hidden",
              {{"hidden", "", ""}, {"visible", "", ""}},
              true,
              {},
              {{"visible", {{"app", "playing"}}}}}}};
}
void native_conditions() {
    WorldContext context(WorldRole::Runtime);
    auto& world = context.world();
    const auto root = world.entity().set<ApplicationStateGate>({true, "app", "game"});
    const auto child = world.entity().child_of(root).set<SpatialBinding>({SpatialMode::World, {}});
    const auto nested =
        world.entity().child_of(child).set<ApplicationStateGate>({true, "hud", "hidden"});
    const auto free = world.entity();
    rejects([&] { ApplicationStateActivation view(context); });
    configure_application_states(world, definition());
    initialize_application_states(world);
    {
        ApplicationStateActivation view(context);
        check(!view.active(root) && !view.active(child) && !view.active(nested) &&
                  view.active(free),
              "Initial condition or structural descendant rule is incorrect");
        check(!view.active(0), "Null entity became active");
    }
    control_application_states(world, {{"domain", "app"}, {"state", "playing"}});
    {
        ApplicationStateActivation view(context);
        check(view.active(child) && view.active(root) && !view.active(nested),
              "Ancestor membership or nested AND condition is incorrect");
    }
    control_application_states(world, {{"domain", "app"}, {"state", "paused"}});
    {
        ApplicationStateActivation view(context);
        check(view.active(nested), "Computed condition did not follow new configuration");
        ApplicationStateActivation moved(std::move(view));
        check(!view.active(child) && moved.active(child), "Moved view lost condition ownership");
        check(std::async(std::launch::async,
                         [&] {
                             try {
                                 (void)moved.active(child);
                             } catch (const std::exception&) {
                                 return true;
                             }
                             return false;
                         })
                  .get(),
              "Condition view entered the world from another thread");
    }
    const auto base =
        world.entity().add(flecs::Prefab).set<ApplicationStateGate>({true, "app", "playing"});
    const auto inherited = world.entity().is_a(base);
    const auto inherited_child = world.entity().child_of(inherited);
    {
        ApplicationStateActivation view(context);
        check(!view.active(inherited_child),
              "Inherited condition did not gate structural children");
    }
    inherited.set<ApplicationStateGate>({false, "app", "playing"});
    {
        ApplicationStateActivation view(context);
        check(view.active(inherited_child),
              "Owned disabled condition did not override inherited value");
    }
    root.disable();
    control_application_states(world, {{"domain", "app"}, {"state", "menu"}});
    {
        ApplicationStateActivation view(context);
        check(!view.active(child), "Disabled native parent lost its condition");
    }
    root.enable();
    auto last = world.entity().child_of(root);
    // ChildOf creation must also fit the pinned native Debug hierarchy limit.
    for (int i = 0; i < FLECS_DAG_DEPTH_MAX - 8; ++i)
        last = world.entity().child_of(last);
    {
        ApplicationStateActivation view(context);
        check(!view.active(last), "Deep structural chain did not use its ancestor condition");
    }
    root.set<ApplicationStateGate>({true, "app", "unknown"});
    rejects([&] { ApplicationStateActivation view(context); });
}
void schema_and_history() {
    WorldContext context;
    Scene scene(context);
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "actor"}, {"name", "Actor"}, {"components", Json::object()}}})}});
    const auto before = scene.document();
    auto after = before;
    after["entities"][0]["components"]["forge.application_state_gate"] = {
        {"enabled", true}, {"domain", "app"}, {"state", "playing"}};
    after["entities"][0]["state_active"] = false;
    scene.edit(after);
    ApplicationStateActivation preview(context);
    check(preview.active(scene.entity(after.at("entities")[0].at("id").get<std::string>())),
          "Authoring preview applied runtime conditions");
    check(!scene.effective_document().at("entities")[0].contains("state_active"),
          "Opaque source metadata overrode derived presentation activity");
    scene.undo();
    check(scene.document() == before, "Undo lost condition source/history");
    scene.redo();
    check(scene.document() == after, "Redo lost condition source/history");
    auto bad = after;
    bad["entities"][0]["components"]["forge.application_state_gate"]["domain"] = "Bad Key";
    rejects([&] { scene.edit(bad); });
    check(scene.document() == after, "Rejected condition changed scene/history");
    bool found = false;
    for (const auto& type : context.schema().at("components"))
        if (type.at("id") == "forge.application_state_gate") {
            found = true;
            check(type.at("optional") == true && type.at("category") == "Gameplay" &&
                      type.at("display_name") == "State Condition",
                  "Condition metadata is not usable");
        }
    check(found, "Condition builtin is not discoverable");
}
void presentation_conditions() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.replace(
        {{"version", 1},
         {"entities",
          Json::array({{{"id", "group"}, {"name", "Group"}, {"components", Json::object()}},
                       {{"id", "camera"},
                        {"name", "Camera"},
                        {"parent", "group"},
                        {"components", Json::object()}}})}});
    const auto group = scene.entity("group");
    const auto camera = scene.entity("camera");
    group.set<ApplicationStateGate>({true, "app", "playing"});
    camera.set<Camera>({});
    camera.set<LocalTranslation>({0, 0, 5});
    configure_application_states(scene.world(), definition());
    initialize_application_states(scene.world());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto source = scene.document();
    auto inactive = simulation.presentation(1);
    check(inactive.at("entities").size() == 2 &&
              !inactive.at("entities")[1].at("state_active").get<bool>() &&
              extract_render_scene(inactive).cameras.empty(),
          "Inactive camera rendered or structural transform rows disappeared");
    control_application_states(scene.world(), {{"domain", "app"}, {"state", "playing"}});
    const auto active = simulation.presentation(1);
    check(extract_render_scene(active).cameras.size() == 1 && camera.get<Camera>().enabled &&
              scene.document() == source,
          "Camera activation changed authored enable fields or scene source");
    control_application_states(scene.world(), {{"domain", "app"}, {"state", "menu"}});
    check(extract_render_scene(simulation.presentation(1)).cameras.empty(),
          "Camera did not leave presentation after state exit");
    check(extract_render_scene(scene.effective_document()).cameras.size() == 1,
          "Plain authoring projection gained runtime-only gating metadata");
    const auto before = group.get<ApplicationStateGate>();
    rejects([&] {
        scene.tune_runtime_property("group", "forge.application_state_gate", "state", "missing");
    });
    check(group.get<ApplicationStateGate>() == before && scene.document() == source,
          "Rejected live condition changed native values or source");
    scene.tune_runtime_property("group", "forge.application_state_gate", "state", "menu");
    check(extract_render_scene(simulation.presentation(1)).cameras.size() == 1,
          "Live semantic condition edit did not update presentation");
}
void physics_conditions() {
    EngineContext engine(WorldRole::Runtime, false, {physics_module()});
    Scene scene(engine.world());
    auto rows = Json::array();
    for (const char* id : {"group", "rigid", "planar", "character", "character2d", "survivor",
                           "planar_survivor", "joint", "joint2d"})
        rows.push_back({{"id", id}, {"name", id}, {"components", Json::object()}});
    scene.replace({{"version", 1}, {"entities", rows}});
    const auto group = scene.entity("group");
    group.set<ApplicationStateGate>({true, "app", "playing"});
    PhysicsBody body;
    body.motion = 2;
    body.gravity_factor = 0;
    body.linear_damping = body.angular_damping = 0;
    body.allow_sleeping = false;
    PhysicsBody2D planar;
    planar.motion = 2;
    planar.gravity_factor = 0;
    planar.allow_sleeping = false;
    unsigned offset = 0;
    for (const char* id :
         {"rigid", "planar", "character", "character2d", "survivor", "planar_survivor"}) {
        auto entity = scene.entity(id);
        entity.set<LocalTranslation>({float(offset++ * 10), 0, 0})
            .set<SpatialBinding>({SpatialMode::World, {}});
        if (std::string_view(id).find("survivor") == std::string_view::npos)
            entity.child_of(group);
    }
    for (const char* id : {"rigid", "survivor"})
        scene.entity(id).set<PhysicsBody>(body).set<BoxCollider>({});
    for (const char* id : {"planar", "planar_survivor"})
        scene.entity(id).set<PhysicsBody2D>(planar).set<BoxCollider2D>({});
    scene.entity("character").set<CharacterController>({});
    scene.entity("character2d").set<CharacterController2D>({});
    PhysicsJoint joint;
    joint.body_a = scene.reference("rigid");
    joint.body_b = scene.reference("survivor");
    joint.min_distance = joint.max_distance = 40;
    scene.entity("joint").set<PhysicsJoint>(joint);
    PhysicsJoint2D joint2d;
    joint2d.body_a = scene.reference("planar");
    joint2d.body_b = scene.reference("planar_survivor");
    joint2d.distance = 40;
    scene.entity("joint2d").set<PhysicsJoint2D>(joint2d);
    configure_application_states(scene.world(), definition());
    initialize_application_states(scene.world());
    RuntimeSimulation simulation(engine.world(), scene);
    const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    simulation.tick(1.f / 60);
    check(physics->status().at("bodies") == 1 && physics->status().at("bodies_2d") == 1,
          "Inactive bodies entered native solvers");
    control_application_states(scene.world(), {{"domain", "app"}, {"state", "playing"}});
    check(!physics->checkpoint_ready(),
          "New state activity produced a premature physics checkpoint");
    rejects([&] { (void)physics->checkpoint(); });
    simulation.tick(1.f / 60);
    check(physics->checkpoint_ready(), "Physics adoption did not restore recovery readiness");
    check(physics->status().at("bodies") == 2 && physics->status().at("bodies_2d") == 3 &&
              physics->status().at("characters") == 1 && physics->status().at("joints") == 1 &&
              physics->status().at("joints_2d") == 1,
          "Activated native bodies, controllers or endpoint-dependent joints are missing");
    {
        const auto saved_physics = physics->checkpoint();
        const auto saved_states = checkpoint_application_states(scene.world());
        EngineContext recovered(WorldRole::Runtime, false, {physics_module()});
        Scene restored(recovered.world());
        RuntimeSimulation restored_simulation(recovered.world(), restored);
        restored.restore_snapshot(scene.snapshot());
        configure_application_states(restored.world(), definition());
        restore_application_states(restored.world(), saved_states);
        restored_simulation.reconcile_state_conditions();
        const auto restored_physics =
            std::static_pointer_cast<PhysicsRuntime>(recovered.services().physics());
        check(!restored_physics->checkpoint_ready(),
              "Unrealized recovery candidate claimed adopted physics activity");
        restored_physics->restore(saved_physics);
        check(restored_physics->checkpoint_ready() &&
                  restored_physics->status().at("bodies") == 2 &&
                  restored_physics->status().at("bodies_2d") == 3,
              "Restored physics retained pending admission or lost realized consumers");
    }
    const auto tick = physics->status().at("tick");
    const auto survivor = scene.reference("survivor");
    const auto before = physics->body_state(survivor);
    physics->set_linear_velocity(scene.reference("rigid"), {2, 0, 0});
    physics->set_linear_velocity(survivor, {4, 0, 0});
    control_application_states(scene.world(), {{"domain", "app"}, {"state", "menu"}});
    check(physics->status().at("tick") == tick && physics->status().at("bodies") == 1 &&
              physics->status().at("bodies_2d") == 1 && physics->status().at("characters") == 0 &&
              physics->status().at("joints") == 0 && physics->status().at("joints_2d") == 0 &&
              physics->body_state(survivor) == before &&
              scene.entity("rigid").get<PhysicsBody>().enabled,
          "Paused exit retained ineligible native consumers or changed survivor pose/time");
    simulation.tick(1.f / 60);
    check(physics->body_state(survivor)->linear_velocity[0] == 4,
          "State retirement lost a surviving queued command");
    control_application_states(scene.world(), {{"domain", "app"}, {"state", "playing"}});
    simulation.tick(1.f / 60);
    check(physics->status().at("bodies") == 2 && physics->status().at("joints_2d") == 1 &&
              scene.entity("rigid").is_alive(),
          "Reactivation failed to realize existing living entities");
}
} // namespace
int main() {
    try {
        native_conditions();
        schema_and_history();
        presentation_conditions();
        physics_conditions();
        std::cout << "State Condition data, schema/history, references and structural activation "
                     "view and native physics admission/retirement/recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
