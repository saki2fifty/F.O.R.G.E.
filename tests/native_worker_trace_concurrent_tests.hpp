#pragma once
#include "flecs_system_trace.hpp"
#include <array>
#include <barrier>
#include <exception>
#include <forge/runtime.hpp>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace forge::test {
// Native lifecycle/type registration is serialized; separately owned worlds
// deliberately overlap actual native dispatches, not their initialization.
template <class Counter, class Check> void worker_trace_concurrent_worlds(Check check) {
    std::mutex lifecycle;
    std::barrier ready(2), retired(2), dispatch(2);
    std::array<std::exception_ptr, 2> errors{};
    struct Fixture {
        EngineContext engine{WorldRole::Runtime, false};
        Scene scene{engine.world()};
        RuntimeSimulation simulation{engine.world(), scene, {4}};
        std::vector<flecs::entity> actors;
        flecs::system update;
        const std::thread::id owner = std::this_thread::get_id();
        bool main_seen = false;
        Fixture(const std::string& name, std::barrier<>& dispatch) {
            auto& world = engine.world().world();
            for (unsigned i = 0; i < 256; ++i)
                actors.push_back(world.entity().template set<Counter>({}));
            update = world.template system<Counter>(name.c_str())
                         .kind(world.lookup("forge.runtime.Gameplay"))
                         .multi_threaded()
                         .each([this, &dispatch](Counter& value) {
                             if (std::this_thread::get_id() == owner && !main_seen) {
                                 main_seen = true;
                                 dispatch.arrive_and_wait();
                             }
                             ++value.updates;
                         });
            update.template add<FixedSimulation>();
            engine.services().profiling(true);
            engine.services().profiling_systems(true);
        }
        ~Fixture() { update.destruct(); }
    };
    const auto run = [&](unsigned index) {
        bool admitted = false, finished = false;
        std::unique_ptr<Fixture> fixture;
        try {
            const auto name = index ? "TraceRealmBeta" : "TraceRealmAlpha";
            {
                std::lock_guard guard(lifecycle);
                fixture = std::make_unique<Fixture>(name, dispatch);
            }
            ready.arrive_and_wait();
            admitted = true;
            for (unsigned tick = 1; tick <= 3; ++tick) {
                fixture->main_seen = false;
                fixture->engine.services().clear_profiles();
                fixture->simulation.tick(1.f / 60);
                std::set<unsigned> stages;
                for (const auto& record : fixture->engine.services().profiles()) {
                    if (!record.contains("native_stage"))
                        continue;
                    check(record.at("tick") == tick &&
                              record.at("name").template get<std::string>().find(name) !=
                                  std::string::npos,
                          "Concurrent world received another runtime's worker record");
                    stages.insert(record.at("native_stage").template get<unsigned>());
                }
                check(stages == std::set<unsigned>{1, 2, 3},
                      "Concurrent capture omitted its native stages");
                for (auto actor : fixture->actors)
                    check(actor.template get<Counter>().updates == tick,
                          "Concurrent trace capture changed numeric gameplay");
            }
            retired.arrive_and_wait();
            finished = true;
        } catch (...) {
            errors[index] = std::current_exception();
            if (!admitted)
                ready.arrive_and_drop();
            if (!finished)
                retired.arrive_and_drop();
            dispatch.arrive_and_drop();
        }
        std::lock_guard guard(lifecycle);
        fixture.reset();
    };
    std::array<std::thread, 2> owners;
    unsigned started = 0;
    try {
        for (; started < owners.size(); ++started)
            owners[started] = std::thread(run, started);
    } catch (...) {
        for (unsigned missing = started; missing < owners.size(); ++missing) {
            ready.arrive_and_drop();
            retired.arrive_and_drop();
            dispatch.arrive_and_drop();
        }
        for (unsigned i = 0; i < started; ++i)
            owners[i].join();
        throw;
    }
    for (auto& owner : owners)
        owner.join();
    for (const auto& error : errors)
        if (error)
            std::rethrow_exception(error);
}
} // namespace forge::test
