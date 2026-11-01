#include "application_state_access.hpp"
#include "runtime_entity_creation.hpp"
#include <forge/assets.hpp>
#include <forge/character_components.hpp>
#include <forge/runtime.hpp>
#include <future>
#include <iostream>

namespace {
using namespace forge;
using namespace forge::detail;
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
    throw std::runtime_error("Invalid state lifetime operation accepted");
}
ApplicationStatesDefinition definition() {
    return {{{"app",
              "",
              "playing",
              {{"game", "", ""}, {"playing", "", "game"}, {"paused", "", "game"}, {"menu", "", ""}},
              false,
              {},
              {}},
             {"hud",
              "",
              "hidden",
              {{"hidden", "", ""}, {"visible", "", ""}},
              true,
              {},
              {{"visible", {{"app", "game"}}}}},
             {"level",
              "",
              "warmup",
              {{"warmup", "", ""}},
              false,
              ApplicationStateCondition{"app", "game"},
              {}}}};
}
struct Fixture {
    EngineContext engine{WorldRole::Runtime};
    Scene scene{engine.world()};
    Fixture() {
        configure_application_states(engine.world().world(), definition());
        initialize_application_states(engine.world().world());
    }
    RuntimeEntityResult spawn(const std::string& name = "Transient") {
        const auto token = request_runtime_entity(engine.world(), "test.scope", {}, name);
        publish_runtime_entities(engine.world());
        auto result = inspect_runtime_entity(engine.world(), "test.scope", token);
        check(result.state == RuntimeEntityState::Ready, "Runtime root did not publish");
        release_runtime_entity(engine.world(), "test.scope", token);
        return result;
    }
    Json bind(EntityRef entity, const char* domain, const char* state) {
        return control_runtime_entity_scope(
            engine.world(), {{"entity", entity}, {"domain", domain}, {"state", state}});
    }
    void transition(const char* state) {
        control_application_states(engine.world().world(), {{"domain", "app"}, {"state", state}});
    }
};
void lifecycle() {
    Fixture f;
    auto& world = f.engine.world().world();
    const auto leaf = f.spawn(), ancestor = f.spawn(), computed = f.spawn(),
               subordinate = f.spawn();
    const auto unscoped = f.spawn();
    f.bind(leaf.reference, "app", "playing");
    const auto first = f.bind(ancestor.reference, "app", "game");
    check(f.bind(ancestor.reference, "app", "game") == first, "Idempotent binding changed state");
    f.bind(computed.reference, "hud", "visible");
    f.bind(subordinate.reference, "level", "warmup");
    rejects([&] { f.bind(ancestor.reference, "app", "playing"); });
    rejects([&] { f.bind(unscoped.reference, "app", "menu"); });
    rejects([&] { f.bind(unscoped.reference, "app", "missing"); });
    const auto child = world.entity().child_of(world.entity(leaf.native_entity));
    bool saw_exit = false;
    auto observer =
        world.observer().with<ApplicationStateDomainTag>().event<ApplicationStateExited>().run(
            [&](flecs::iter& it) {
                while (it.next()) {
                    const auto& event = *it.param<ApplicationStateExited>();
                    if (event.domain == "app" && event.state == "playing") {
                        saw_exit = true;
                        check(!world.is_alive(leaf.native_entity) && !child.is_alive(),
                              "Exit observer ran before scoped structural retirement");
                        check(application_state_is(world, "app", "paused"),
                              "Retirement preceded complete state publication");
                    }
                }
            });
    f.transition("playing");
    check(world.is_alive(leaf.native_entity), "Same-leaf command retired root");
    f.transition("paused");
    check(saw_exit && world.is_alive(ancestor.native_entity) &&
              world.is_alive(computed.native_entity),
          "Common ancestor or computed scope retired during sibling transition");
    f.transition("menu");
    for (auto root : {ancestor.native_entity, computed.native_entity, subordinate.native_entity})
        check(!world.is_alive(root), "Exited ancestor/computed/subordinate root survived");
    check(world.is_alive(unscoped.native_entity), "Unscoped entity retired");
    f.transition("playing");
    const auto subsequent = f.spawn("After reentry");
    check(f.scene.entity_count() == 2 && world.is_alive(subsequent.native_entity),
          "Later append resurrected retired roots or lost unscoped entity");
    observer.destruct();
}
void admission() {
    Fixture f;
    auto& context = f.engine.world();
    const auto token = request_runtime_entity(context, "test.scope", {}, "Pending");
    Json command{{"request", token}, {"domain", "app"}, {"state", "game"}};
    rejects([&] { control_runtime_entity_scope(context, command, "test.scope"); });
    publish_runtime_entities(context);
    rejects([&] { control_runtime_entity_scope(context, command, "test.foreign"); });
    auto stale = command;
    stale["expected_revision"] = std::uint64_t{0};
    rejects([&] { control_runtime_entity_scope(context, stale, "test.scope"); });
    const auto ready = inspect_runtime_entity(context, "test.scope", token);
    check(application_state_lifetime(context.world(), ready.native_entity).is_null(),
          "Rejected scope changed native root");
    control_runtime_entity_scope(context, command, "test.scope");
    release_runtime_entity(context, "test.scope", token);
    rejects([&] { control_runtime_entity_scope(context, command, "test.scope"); });
    check(context.world().is_alive(ready.native_entity), "Observation release destroyed root");
    check(std::async(std::launch::async,
                     [&] {
                         try {
                             f.bind(ready.reference, "app", "game");
                         } catch (const std::exception&) {
                             return true;
                         }
                         return false;
                     })
              .get(),
          "Foreign thread bound a state lifetime");
    auto authored = f.scene.document();
    const auto id = EntityId::generate();
    authored["entities"].push_back(
        {{"id", id}, {"name", "Authored"}, {"components", Json::object()}});
    f.scene.replace(authored);
    rejects([&] { f.bind(f.scene.reference(id.str()), "app", "game"); });
    Json wire{{"generation", std::uint64_t{8}},
              {"expected_revision", std::uint64_t{1}},
              {"entity", ready.reference},
              {"domain", "app"},
              {"state", "game"}};
    rejects([&] { ApplicationStateAccess::control(context, wire, 9); });
    ApplicationStateAccess::control(context, wire, 8);
}
void recovery() {
    Fixture f;
    const auto bound = f.spawn(), free = f.spawn();
    f.bind(bound.reference, "app", "game");
    const auto scene = f.scene.snapshot();
    const auto states = checkpoint_application_states(f.engine.world().world());
    const auto roots = checkpoint_runtime_entity_lifetimes(f.engine.world());
    EngineContext fresh(WorldRole::Runtime);
    Scene restored(fresh.world());
    restored.restore_snapshot(scene);
    configure_application_states(fresh.world().world(), definition());
    restore_application_states(fresh.world().world(), states);
    auto invalid = roots;
    invalid["roots"].push_back(invalid["roots"][0]);
    rejects([&] { restore_runtime_entity_lifetimes(fresh.world(), invalid); });
    check(checkpoint_runtime_entity_lifetimes(fresh.world()).at("roots").empty(),
          "Rejected recovery installed partial root markers");
    restore_runtime_entity_lifetimes(fresh.world(), roots);
    control_runtime_entity_scope(
        fresh.world(), {{"entity", free.reference}, {"domain", "app"}, {"state", "playing"}});
    control_application_states(fresh.world().world(), {{"domain", "app"}, {"state", "menu"}});
    check(restored.entity_count() == 0, "Recovered root markers/lifetimes did not retire");
    check(f.scene.entity_count() == 2, "Recovery altered original independent world");
}
void prefab_and_disabled_roots() {
    Fixture f;
    const auto asset = AssetId::generate();
    const auto root_id = PrefabMemberId::generate(), child_id = PrefabMemberId::generate();
    const Json source{
        {"format", "forge.prefab"},
        {"version", 3},
        {"asset_id", asset},
        {"revision", 1u},
        {"root", root_id},
        {"members",
         Json::array({{{"id", root_id}, {"name", "Root"}, {"components", Json::object()}},
                      {{"id", child_id},
                       {"parent", root_id},
                       {"name", "Child"},
                       {"components", Json::object()}}})}};
    f.scene.set_prefab_sources({{asset, source}});
    const auto token = request_runtime_prefab(f.engine.world(), "test.scope", {}, asset, "Group");
    publish_runtime_entities(f.engine.world());
    const auto result = inspect_runtime_entity(f.engine.world(), "test.scope", token);
    check(result.state == RuntimeEntityState::Ready && f.scene.entity_count() == 2,
          "Real Prefab group did not publish");
    f.bind(result.reference, "app", "game");
    release_runtime_entity(f.engine.world(), "test.scope", token);
    f.engine.world().world().entity(result.native_entity).disable();
    check(checkpoint_runtime_entity_lifetimes(f.engine.world()).at("roots").size() == 1,
          "Disabled runtime root disappeared from recovery");
    f.transition("menu");
    check(f.scene.entity_count() == 0 && f.scene.document().at("entities").empty(),
          "Disabled scoped Prefab or orphaned member intent survived exit");
    check(f.scene.prefab_sources().contains(asset), "Retirement discarded admitted Prefab source");
    f.transition("playing");
    const auto next =
        request_runtime_prefab(f.engine.world(), "test.scope", {}, asset, "New group");
    publish_runtime_entities(f.engine.world());
    check(inspect_runtime_entity(f.engine.world(), "test.scope", next).state ==
                  RuntimeEntityState::Ready &&
              f.scene.entity_count() == 2 && !f.scene.entity(result.reference.entity.str()),
          "New Prefab publication resurrected an old group");
}
void paused_native_consumers(const std::filesystem::path& gallery,
                             const std::filesystem::path& scratch) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    std::filesystem::copy_file(gallery / "Assets/FalloffTone.wav", root / "Assets/tone.wav");
    const auto clip = AssetCatalog::register_audio_clip(root, "Assets/tone.wav").id;
    EngineContext engine(WorldRole::Runtime, false,
                         {physics_module(), audio_module({root, AudioOutput::Offline, true})});
    Scene scene(engine.world());
    RuntimeSimulation simulation(engine.world(), scene);
    auto& context = engine.world();
    auto& world = context.world();
    configure_application_states(world, definition());
    initialize_application_states(world);
    auto spawn = [&](const char* name) {
        const auto token = request_runtime_entity(context, "test.consumers", {}, name);
        publish_runtime_entities(context);
        const auto value = inspect_runtime_entity(context, "test.consumers", token);
        check(value.state == RuntimeEntityState::Ready, "Consumer root did not publish");
        release_runtime_entity(context, "test.consumers", token);
        return value;
    };
    const auto rigid = spawn("Scoped rigid"), planar = spawn("Scoped planar"),
               survivor = spawn("Rigid survivor"), planar_survivor = spawn("Planar survivor"),
               sound = spawn("Scoped sound"), character = spawn("Scoped character"),
               character2d = spawn("Scoped planar character");
    PhysicsBody body;
    body.motion = 2;
    body.gravity_factor = 0;
    body.linear_damping = body.angular_damping = 0;
    body.allow_sleeping = false;
    PhysicsBody2D body2d;
    body2d.motion = 2;
    body2d.gravity_factor = 0;
    body2d.allow_sleeping = false;
    world.entity(rigid.native_entity)
        .set<LocalTranslation>({})
        .set<PhysicsBody>(body)
        .set<BoxCollider>({});
    world.entity(survivor.native_entity)
        .set<LocalTranslation>({10, 0, 0})
        .set<PhysicsBody>(body)
        .set<BoxCollider>({});
    world.entity(planar.native_entity)
        .set<LocalTranslation>({3, 0, 0})
        .set<PhysicsBody2D>(body2d)
        .set<BoxCollider2D>({});
    world.entity(planar_survivor.native_entity)
        .set<LocalTranslation>({13, 0, 0})
        .set<PhysicsBody2D>(body2d)
        .set<BoxCollider2D>({});
    for (const auto& actor : {rigid, planar, survivor, planar_survivor})
        world.entity(actor.native_entity).set<SpatialBinding>({SpatialMode::World, {}});
    world.entity(character.native_entity)
        .set<LocalTranslation>({20, 0, 0})
        .set<SpatialBinding>({SpatialMode::World, {}})
        .set<CharacterController>({});
    world.entity(character2d.native_entity)
        .set<LocalTranslation>({30, 0, 0})
        .set<SpatialBinding>({SpatialMode::World, {}})
        .set<CharacterController2D>({});
    const auto joint3d = spawn("Scoped constraint"), joint2d = spawn("Scoped planar constraint");
    PhysicsJoint constraint;
    constraint.body_a = rigid.reference;
    constraint.body_b = survivor.reference;
    constraint.min_distance = constraint.max_distance = 10;
    PhysicsJoint2D constraint2d;
    constraint2d.body_a = planar.reference;
    constraint2d.body_b = planar_survivor.reference;
    constraint2d.distance = 10;
    world.entity(joint3d.native_entity)
        .child_of(world.entity(rigid.native_entity))
        .set<PhysicsJoint>(constraint);
    world.entity(joint2d.native_entity)
        .child_of(world.entity(planar.native_entity))
        .set<PhysicsJoint2D>(constraint2d);
    AudioSource source;
    source.clip.id = clip;
    source.loop = true;
    source.play_on_start = true;
    source.spatialized = false;
    world.entity(sound.native_entity).set<AudioSource>(source);
    auto audio = std::static_pointer_cast<AudioRuntime>(engine.services().audio());
    check(audio->wait_for_sources(std::chrono::seconds(5)), "Scoped audio preparation timed out");
    simulation.tick(1.f / 60);
    auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
    check(physics->status().at("bodies") == 2 && physics->status().at("bodies_2d") == 3 &&
              physics->status().at("characters") == 1 && physics->status().at("joints") == 1 &&
              physics->status().at("joints_2d") == 1 && audio->status().at("source_voices") == 1,
          "Native consumers did not adopt fixtures");
    for (const auto& actor : {rigid, sound, character})
        control_runtime_entity_scope(
            context, {{"entity", actor.reference}, {"domain", "app"}, {"state", "playing"}});
    control_runtime_entity_scope(
        context, {{"entity", planar.reference}, {"domain", "app"}, {"state", "game"}});
    control_runtime_entity_scope(
        context, {{"entity", character2d.reference}, {"domain", "app"}, {"state", "game"}});
    const auto before3d = physics->body_state(survivor.reference);
    const auto before2d = physics->body_state_2d(planar_survivor.reference);
    const auto tick = physics->status().at("tick");
    physics->set_linear_velocity(rigid.reference, {2, 0, 0});
    physics->set_linear_velocity_2d(planar.reference, {2, 0});
    physics->set_linear_velocity(survivor.reference, {4, 0, 0});
    physics->set_linear_velocity_2d(planar_survivor.reference, {5, 0});
    physics->move_character(character.reference, {1, 0, 0});
    physics->move_character_2d(character2d.reference, {1, 0});
    control_application_states(world, {{"domain", "app"}, {"state", "paused"}});
    check(physics->checkpoint_ready() && physics->status().at("bodies") == 1 &&
              physics->status().at("bodies_2d") == 3 && physics->status().at("characters") == 0,
          "Paused 3D retirement did not leave a synchronized checkpoint");
    (void)physics->checkpoint();
    control_application_states(world, {{"domain", "app"}, {"state", "menu"}});
    check(physics->status().at("bodies") == 1 && physics->status().at("bodies_2d") == 1 &&
              physics->status().at("joints") == 0 && physics->status().at("joints_2d") == 0 &&
              physics->status().at("tick") == tick && audio->status().at("source_voices") == 0,
          "Paused retirement left ghost physics/audio consumers or advanced simulation");
    check(physics->body_state(survivor.reference) == before3d &&
              physics->body_state_2d(planar_survivor.reference) == before2d,
          "Retirement reapplied poses or consumed survivor commands");
    check(!physics->checkpoint_ready(), "Unadopted 2D retirement became a checkpoint");
    rejects([&] { (void)physics->checkpoint(); });
    simulation.tick(1.f / 60);
    check(physics->checkpoint_ready(), "Step did not complete retirement event adoption");
    (void)physics->checkpoint();
    check(physics->body_state(survivor.reference)->linear_velocity[0] == 4 &&
              physics->body_state_2d(planar_survivor.reference)->linear_velocity[0] == 5,
          "Destroyed target commands survived or survivor commands were discarded");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and private scratch");
        lifecycle();
        admission();
        recovery();
        prefab_and_disabled_roots();
        paused_native_consumers(argv[1], argv[2]);
        std::cout << "State lifetimes: structural retirement, ancestors, derived domains, "
                     "admission and recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
