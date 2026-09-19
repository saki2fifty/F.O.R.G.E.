#pragma once
#include <forge/world.hpp>
#include <stdexcept>
struct FlecsContractPosition {
    double x;
};
struct FlecsContractVelocity {
    double x;
};
struct FlecsContractEvent {};
struct FlecsContractTracked {
    double x;
};
inline void test_flecs_contracts() {
    using P = FlecsContractPosition;
    using V = FlecsContractVelocity;
    auto check = [](bool ok, const char* message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    forge::EngineContext engine(forge::WorldRole::Preview);
    auto& world = engine.world().world();
    world.component<P>().add(flecs::CanToggle);
    world.component<V>().add(flecs::Sparse);
    auto entity = world.entity().set<P>({1}).set<V>({2});
    const V* address = &entity.get<V>();
    auto temporary = world.entity();
    entity.add(temporary);
    check(&entity.get<V>() == address, "Sparse component address moved across table transition");
    entity.disable<P>();
    entity.enable<P>(); // First toggle installs native toggle storage.
    const auto* table = ecs_get_table(world, entity);
    entity.disable<P>();
    auto active = world.query<const P>();
    check(active.count() == 0 && entity.has<P>() && ecs_get_table(world, entity) == table,
          "CanToggle changed component storage or failed query filtering");
    entity.enable<P>();
    check(active.count() == 1, "CanToggle did not re-enable matching");
    entity.set<FlecsContractTracked>({1});
    auto tracked = world.query_builder<const FlecsContractTracked>().detect_changes().build();
    check(tracked.changed(), "First matching table was not dirty");
    tracked.each([](const FlecsContractTracked&) {});
    check(!tracked.changed(), "Read-only iteration failed to acknowledge change");
    entity.get_mut<FlecsContractTracked>().x = 3;
    check(!tracked.changed(), "Raw mutable write unexpectedly self-notified");
    entity.modified<FlecsContractTracked>();
    check(tracked.changed(), "Explicit modified failed to invalidate tracked query");
    int enters = 0, leaves = 0, events = 0;
    auto monitor =
        world.observer<P, V>().event(flecs::Monitor).each([&](flecs::iter& it, size_t, P&, V&) {
            if (it.event() == flecs::OnAdd)
                ++enters;
            else
                ++leaves;
        });
    auto event = world.observer<P>().event<FlecsContractEvent>().each([&](P&) { ++events; });
    auto second = world.entity().set<P>({4});
    world.defer_begin();
    second.set<V>({5});
    check(!second.has<V>() && enters == 0, "Deferred mutation escaped its merge boundary");
    world.defer_end();
    check(enters == 1, "Monitor did not observe component combination entering");
    world.event<FlecsContractEvent>().id<P>().entity(second).emit();
    check(events == 1, "Native ECS event did not dispatch");
    second.remove<V>();
    check(leaves == 1, "Monitor did not observe leaving combination");
    monitor.destruct();
    event.destruct(); // Captured counters outlive callback registration.
    auto relation = world.entity().add(flecs::DontFragment);
    auto target_a = world.entity(), target_b = world.entity();
    entity.add(relation, target_a);
    const auto* relation_table = ecs_get_table(world, entity);
    entity.add(relation, target_b);
    check(ecs_get_table(world, entity) == relation_table && entity.has(relation, target_b),
          "DontFragment relationship target fragmented entity storage");
    auto matches = world.query_builder().with(relation, flecs::Wildcard).build();
    check(matches.count() == 2, "Native wildcard failed sparse relationship matching");
    auto exclusive = world.entity().add(flecs::Exclusive).add(flecs::OnDeleteTarget, flecs::Remove);
    entity.add(exclusive, target_a).add(exclusive, target_b);
    check(!entity.has(exclusive, target_a) && entity.has(exclusive, target_b),
          "Exclusive trait failed target replacement");
    target_b.destruct();
    check(entity.is_alive() && !entity.has(exclusive, flecs::Wildcard),
          "Remove cleanup damaged source entity");
    {
        // Reserve native ranges before any deletion/recycling. This is a standalone
        // host setup probe; FORGE currently allocates ordinary host-world IDs.
        flecs::world ranged_world;
        const auto* range = ecs_entity_range_new(ranged_world, 100000, 100010);
        const auto* previous_range = ecs_entity_range_get(ranged_world);
        ecs_entity_range_set(ranged_world, range);
        auto ranged = ranged_world.entity();
        const auto raw = ranged.id() & ECS_ENTITY_MASK;
        check(raw >= 100000 && raw <= 100010, "Native range failed allocation bounds");
        const auto old_generation = ranged.id();
        ranged.destruct();
        auto reused = ranged_world.entity();
        check((reused.id() & ECS_ENTITY_MASK) >= 100000 && reused.id() != old_generation,
              "Range recycling lost generation/bounds");
        ecs_set_version(ranged_world, (reused.id() & ECS_ENTITY_MASK) | (ecs_entity_t{17} << 32));
        check(!ranged_world.is_alive(reused.id()),
              "Manual native generation did not retire previous handle");
        ecs_entity_range_set(ranged_world, previous_range);
    }
    {
        int ticks = 0;
        flecs::world app_world;
        app_world.system().run([&](flecs::iter& it) {
            while (it.next())
                ++ticks;
        });
        ecs_frame_begin(app_world, .01f);
        ecs_frame_end(app_world);
        ecs_app_desc_t app{};
        app.frames = 3;
        app.delta_time = .01f;
        check(ecs_app_run(app_world, &app) == 0 && ticks == 3 && ecs_should_quit(app_world),
              "Native App/Frame finite host loop failed");
        // Pinned app.c sets quit; this C entrypoint does not finalize the world.
    }
    const auto stale = second.id();
    second.destruct();
    auto recycled = world.entity();
    check(!world.is_alive(stale) && recycled.id() != stale,
          "Entity generation failed to invalidate retired native handle");
}
