#include "runtime_execution_values.hpp"
#include <atomic>
#include <cmath>
#include <forge/project.hpp>
#include <forge/runtime.hpp>
#include <iostream>
#include <thread>
#include <vector>
using namespace forge;
namespace {
struct NumericState {
    double value = 0;
    unsigned updates = 0;
};
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Operation> void rejects(Operation operation) {
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid worker configuration admitted");
}
std::vector<double> exercise(unsigned workers) {
    EngineContext engine(WorldRole::Runtime, false);
    auto& context = engine.world();
    auto& world = context.world();
    const auto owner = std::this_thread::get_id();
    Scene scene(context);
    std::vector<flecs::entity> entities;
    for (unsigned i = 0; i < 2048; ++i)
        entities.push_back(world.entity().set<NumericState>({double(i), 0}));
    std::atomic<bool> worker_seen = false, observer_wrong_thread = false;
    std::atomic<unsigned> notifications = 0;
    auto observer =
        world.observer<const NumericState>().event(flecs::OnSet).each([&](const NumericState&) {
            if (std::this_thread::get_id() != owner)
                observer_wrong_thread = true;
            ++notifications;
        });
    const auto previous_pipeline = world.get_pipeline();
    const bool previous_timing = (ecs_world_get_flags(world) & EcsWorldMeasureSystemTime) != 0;
    {
        RuntimeSimulation simulation(context, scene, {workers});
        check(world.get_threads() == int(workers), "Real runtime did not configure native stages");
        check(bool(ecs_world_get_flags(world) & EcsWorldMeasureSystemTime) ==
                  (workers == 1 && previous_timing),
              "Parallel runtime left the shared native per-system counter enabled");
        auto update = world.system<NumericState>("project.WorkerUpdate")
                          .kind(world.lookup("forge.runtime.Gameplay"))
                          .multi_threaded()
                          .each([&](flecs::entity entity, NumericState& state) {
                              if (std::this_thread::get_id() != owner)
                                  worker_seen = true;
                              state.value += .25;
                              ++state.updates;
                              entity.modified<NumericState>();
                          });
        update.add<FixedSimulation>();
        unsigned serial_calls = 0;
        bool serial_wrong_thread = false, serial_wrong_values = false;
        auto serial = world.system<const NumericState>("project.SerialRead")
                          .kind(world.lookup("forge.runtime.Gameplay"))
                          .each([&](const NumericState& state) {
                              serial_wrong_thread |= std::this_thread::get_id() != owner;
                              serial_wrong_values |=
                                  state.updates != serial_calls / entities.size() + 1;
                              ++serial_calls;
                          });
        serial.add<FixedSimulation>();
        engine.services().profiling(true);
        (void)simulation.system_profile();
        RuntimeClock clock;
        const auto tick = [&](float delta) { simulation.tick(delta); };
        for (unsigned i = 0; i < 5; ++i)
            clock.step(tick);
        check(clock.tick() == 5 && clock.paused(), "Workers changed Step/Pause clock ownership");
        const auto calls_before = serial_calls;
        clock.advance(RuntimeClock::Time{} + std::chrono::seconds(1), tick);
        check(serial_calls == calls_before, "Paused clock executed worker gameplay");
        check(!serial_wrong_thread && !serial_wrong_values && serial_calls == entities.size() * 5,
              "Nonparallel system lost owner-thread execution or declared producer ordering");
        check(!observer_wrong_thread && notifications == entities.size() * 5,
              "Staged modifications escaped owner-thread merge or lost notifications");
        check(worker_seen == (workers > 1), "Parallel system did not use the expected OS threads");
#ifndef FORGE_DISABLE_PROFILING
        const auto profile = simulation.system_profile();
        check(profile.at("available") && profile.value("ecs_workers", 1u) == workers,
              "Native plan did not report the actual stage configuration");
        if (workers > 1) {
            check(!profile.at("native_system_timing").get<bool>() &&
                      profile.at("system_trace_scope") == "owner_thread",
                  "Parallel native plan overstated profiling coverage");
            for (const auto& row : profile.at("entries"))
                if (row.at("kind") == "system")
                    check(row.at("cpu_seconds").is_null(),
                          "Parallel native plan exposed a shared per-system timing counter");
        }
#endif
        update.destruct();
        serial.destruct();
    }
    check(world.get_threads() == 1 && world.get_pipeline() == previous_pipeline,
          "Runtime retirement did not join owned stages and restore its prior pipeline");
    check(bool(ecs_world_get_flags(world) & EcsWorldMeasureSystemTime) == previous_timing,
          "Runtime retirement did not restore the caller's native timing configuration");
    observer.destruct();
    std::vector<double> result;
    for (unsigned i = 0; i < entities.size(); ++i) {
        const auto& state = entities[i].get<NumericState>();
        check(state.value == double(i) + 1.25 && state.updates == 5,
              "Native worker partition duplicated or omitted an entity");
        result.push_back(state.value);
    }
    return result;
}
void admission() {
    for (unsigned workers : {0u, 17u, ~0u})
        rejects([&] { RuntimeExecutionConfig{workers}.validate(); });
    auto project = ProjectSettings::defaults("Worker validation");
    check(project.at("ecs_workers") == 1, "Project default enabled parallel execution");
    for (const Json& invalid : {Json(), Json(false), Json(0), Json(-1), Json(17), Json(4.),
                                Json("4"), Json(std::uint64_t{1} << 32)}) {
        auto candidate = project;
        candidate["ecs_workers"] = invalid;
        rejects([&] { ProjectSettings::validate(candidate); });
    }
    project.erase("ecs_workers");
    ProjectSettings::validate(project);
    check(detail::read_runtime_execution(project).ecs_workers == 1,
          "Legacy project lost its sequential default");
    EngineContext engine(WorldRole::Runtime, false);
    auto& context = engine.world();
    Scene scene(context);
    context.world().set_threads(2);
    const auto pipeline = context.world().get_pipeline();
    rejects([&] { RuntimeSimulation rejected(context, scene, {4}); });
    check(context.world().get_threads() == 2 && context.world().get_pipeline() == pipeline,
          "Rejected worker ownership changed an existing native configuration");
    context.world().set_threads(1);
}
Json measure() {
    Json measurements = Json::array();
    for (unsigned work : {0u, 64u}) {
        std::vector<double> reference;
        for (unsigned pair = 0; pair < 3; ++pair)
            for (unsigned workers :
                 (pair % 2 ? std::vector<unsigned>{4, 1} : std::vector<unsigned>{1, 4})) {
                EngineContext engine(WorldRole::Runtime, false);
                auto& context = engine.world();
                auto& world = context.world();
                Scene scene(context);
                std::vector<flecs::entity> entities;
                for (unsigned i = 0; i < 8192; ++i)
                    entities.push_back(world.entity().set<NumericState>({double(i) / 8192, 0}));
                RuntimeSimulation simulation(context, scene, {workers});
                auto update = world.system<NumericState>("project.MeasuredNumericWork")
                                  .kind(world.lookup("forge.runtime.Gameplay"))
                                  .multi_threaded()
                                  .each([work](NumericState& state) {
                                      state.value += .0001;
                                      for (unsigned i = 0; i < work; ++i)
                                          state.value = std::sin(state.value + .001) + .001;
                                      ++state.updates;
                                  });
                update.add<FixedSimulation>();
                for (unsigned i = 0; i < 4; ++i)
                    simulation.tick(1.f / 60);
                const auto started = std::chrono::steady_clock::now();
                for (unsigned i = 0; i < 30; ++i)
                    simulation.tick(1.f / 60);
                const auto elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
                        .count();
                std::vector<double> values;
                for (auto entity : entities) {
                    const auto& value = entity.get<NumericState>();
                    check(value.updates == 34, "Measured worker workload missed an entity update");
                    values.push_back(value.value);
                }
                if (reference.empty())
                    reference = values;
                check(values == reference, "Measured workload diverged from its numeric oracle");
                measurements.push_back({{"ecs_workers", workers},
                                        {"pair", pair},
                                        {"entities", entities.size()},
                                        {"kernel_iterations", work},
                                        {"warmup_ticks", 4},
                                        {"measured_ticks", 30},
                                        {"wall_seconds", elapsed},
                                        {"exact_values", true}});
                update.destruct();
            }
    }
    return measurements;
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--measure") {
            std::cout << measure().dump(2) << '\n';
            return 0;
        }
        check(argc == 1, "Unknown worker test option");
        admission();
        const auto sequential = exercise(1);
        check(exercise(2) == sequential && exercise(4) == sequential,
              "Real fixed-pipeline worker results diverged from the sequential reference");
        std::cout
            << "Real runtime1/2/4 native stages, fixed clock, merges/order and teardown passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
