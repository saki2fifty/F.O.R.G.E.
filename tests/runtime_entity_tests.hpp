#pragma once
#include "../src/runtime_entity_creation.hpp"
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <future>
inline void test_runtime_entities() {
    using namespace forge;
    using namespace forge::detail;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto rejected = [](auto&& operation) {
        try {
            operation();
        } catch (const std::exception&) {
            return true;
        }
        return false;
    };
    EngineContext engine(WorldRole::Runtime);
    auto& context = engine.world();
    auto& world = context.world();
    const std::string module = "project.spawn";
    const auto before_scene = request_runtime_entity(context, module, {}, "Early request");
    check(inspect_runtime_entity(context, module, before_scene).state ==
              RuntimeEntityState::Pending,
          "Startup request must wait for a scene and fixed boundary");
    auto scene = std::make_unique<Scene>(context);
    Module legacy;
    auto simulation = std::make_unique<RuntimeSimulation>(context, *scene, legacy);
    RuntimeClock clock;
    auto tick = [&](float dt) { simulation->tick(dt); };
    clock.advance(RuntimeClock::Time{} + std::chrono::seconds(10), tick);
    check(scene->entity_count() == 0, "Paused wall time published a runtime entity");
    clock.step(tick);
    auto early = inspect_runtime_entity(context, module, before_scene);
    check(early.state == RuntimeEntityState::Ready && early.reference.scene == scene->asset_id() &&
              early.native_entity && !scene->can_undo(),
          "Step did not publish registered runtime entity without history");
    struct NativeState {
        int value = 42;
    };
    auto original = world.entity(early.native_entity);
    original.set<NativeState>({42}).set<LocalTranslation>({1, 2, 3});
    int writes = 0;
    auto observer = world.observer<const LocalTranslation>()
                        .event(flecs::OnSet)
                        .each([&](flecs::entity entity, const LocalTranslation&) {
                            if (entity == original)
                                ++writes;
                        });
    std::uint64_t deferred_token = 0;
    auto system =
        world.system().kind(world.lookup("forge.runtime.Gameplay")).run([&](flecs::iter&) {
            if (!deferred_token) {
                check(ecs_is_deferred(world), "Fixture must run deferred");
                deferred_token = request_runtime_entity(context, module, {}, "Deferred");
                check(rejected([&] { publish_runtime_entities(context); }),
                      "Publication allowed inside deferred Flecs progress");
            }
        });
    system.add<FixedSimulation>();
    clock.step(tick);
    check(inspect_runtime_entity(context, module, deferred_token).state ==
              RuntimeEntityState::Pending,
          "Deferred request published inside requesting tick");
    clock.step(tick);
    const auto deferred = inspect_runtime_entity(context, module, deferred_token);
    check(deferred.state == RuntimeEntityState::Ready && scene->entity_count() == 2 &&
              original.get<NativeState>().value == 42 && writes == 0 &&
              !original.owns<LocalRotation>() && !original.owns<LocalScale>(),
          "Runtime append changed unrelated native state or TRS ownership");
    MeshRenderer mesh;
    mesh.mesh.id = AssetId::generate();
    world.entity(deferred.native_entity).set<MeshRenderer>(mesh).set<LocalTranslation>({4, 5, 6});
    clock.step(tick);
    const auto rendered = extract_render_scene(simulation->presentation(1));
    check(std::any_of(rendered.meshes.begin(), rendered.meshes.end(),
                      [&](const auto& item) {
                          return item.entity == deferred.reference.entity &&
                                 item.renderer.mesh == mesh.mesh && item.world.m[3] == 4 &&
                                 item.world.m[7] == 5 && item.world.m[11] == 6;
                      }),
          "Registered runtime render component not presented");
    check(!scene->can_undo() && !scene->can_redo(), "Runtime creation wrote authoring history");
    check(release_runtime_entity(context, module, deferred_token) &&
              world.is_alive(deferred.native_entity),
          "Release destroyed a completed entity");
    check(rejected([&] { inspect_runtime_entity(context, module, deferred_token); }),
          "Released request token accepted");
    const auto cancelled = request_runtime_entity(context, module, {}, "Cancelled");
    check(!release_runtime_entity(context, "project.foreign", cancelled) &&
              rejected([&] { inspect_runtime_entity(context, "project.foreign", cancelled); }),
          "Foreign module request token accepted");
    check(release_runtime_entity(context, module, cancelled), "Cancellation failed");
    clock.step(tick);
    check(scene->entity_count() == 2, "Cancelled entity was created");
    check(std::async(std::launch::async,
                     [&] {
                         return rejected([&] {
                                    request_runtime_entity(context, module, {}, "Wrong thread");
                                }) &&
                                rejected(
                                    [&] { inspect_runtime_entity(context, module, before_scene); });
                     })
              .get(),
          "Runtime entity queue accepted a foreign thread");
    for (const auto& name :
         {std::string{}, std::string(256, 'x'), std::string("a\0b", 3), std::string("\xff", 1)})
        check(rejected([&] { request_runtime_entity(context, module, {}, name); }),
              "Invalid name accepted before publication");
    const auto missing = request_runtime_entity(context, module, AssetId::generate(), "Missing");
    clock.step(tick);
    check(inspect_runtime_entity(context, module, missing).state == RuntimeEntityState::Failed &&
              scene->entity_count() == 2,
          "Missing scene did not fail without mutation");
    release_runtime_entity(context, module, missing);
    {
        Scene second(context);
        const auto ambiguous = request_runtime_entity(context, module, {}, "Ambiguous");
        const auto explicit_scene =
            request_runtime_entity(context, module, second.asset_id(), "Explicit");
        clock.step(tick);
        check(inspect_runtime_entity(context, module, ambiguous).state ==
                      RuntimeEntityState::Failed &&
                  inspect_runtime_entity(context, module, explicit_scene).state ==
                      RuntimeEntityState::Ready &&
                  second.entity_count() == 1 && scene->entity_count() == 2,
              "Unique/explicit runtime scene selection failed");
        release_runtime_entity(context, module, ambiguous);
        release_runtime_entity(context, module, explicit_scene);
        second.reset(scene->document());
        const auto repeated =
            request_runtime_entity(context, module, scene->asset_id(), "Repeated instance");
        clock.step(tick);
        check(inspect_runtime_entity(context, module, repeated).state == RuntimeEntityState::Failed,
              "Asset identity was mistaken for a unique loaded scene instance");
        release_runtime_entity(context, module, repeated);
    }
    {
        EngineContext other(WorldRole::Runtime);
        check(rejected([&] { inspect_runtime_entity(other.world(), module, before_scene); }),
              "Cross-world token accepted");
    }
    for (auto role : {WorldRole::Authoring, WorldRole::Validation}) {
        EngineContext other(role);
        check(rejected([&] { request_runtime_entity(other.world(), module, {}, "Wrong role"); }),
              "Non-runtime entity creation accepted");
    }
    observer.destruct();
    system.destruct();
    simulation.reset();
    const auto saved_scene = scene->document();
    scene.reset();
    check(inspect_runtime_entity(context, module, before_scene).state == RuntimeEntityState::Gone,
          "Unloaded scene retained a ready native entity");
    scene = std::make_unique<Scene>(context);
    scene->reset(saved_scene);
    check(inspect_runtime_entity(context, module, before_scene).state == RuntimeEntityState::Gone,
          "Old request retargeted a reloaded instance");
    release_runtime_entity(context, module, before_scene);
    std::vector<std::pair<std::string, std::uint64_t>> limit;
    for (unsigned owner = 0; owner < 4; ++owner) {
        const auto name = "project.owner" + std::to_string(owner);
        for (unsigned i = 0; i < 64; ++i)
            limit.emplace_back(name, request_runtime_entity(context, name, {}, "Bounded"));
        check(rejected([&] { request_runtime_entity(context, name, {}, "Excess"); }),
              "Per-module runtime request limit ignored");
    }
    check(rejected([&] { request_runtime_entity(context, "project.extra", {}, "Excess"); }),
          "Per-world runtime request limit ignored");
    for (const auto& [owner, token] : limit)
        release_runtime_entity(context, owner, token);
    publish_runtime_entities(context);
    check(scene->entity_count() == 2, "Cancelled bounded batch mutated scene");
}
