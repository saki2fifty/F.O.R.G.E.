#include "cpu_profile_trace.hpp"
#include "flecs_system_trace.hpp"
#include "native_worker_trace_concurrent_tests.hpp"
#include "native_worker_trace_lifetime_tests.hpp"
#include <atomic>
#include <cmath>
#include <forge/runtime.hpp>
#include <iostream>
#include <set>
#include <thread>
#include <vector>
using namespace forge;
namespace {
struct TraceCounter {
    unsigned updates = 0;
};
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
enum class ProbeMode { Normal, Unmatched, Capacity, ClearDuringTick };
void exercise(bool custom_clock, bool tracing, ProbeMode mode = ProbeMode::Normal) {
    const auto owner = std::this_thread::get_id();
    std::atomic<bool> foreign_clock = false;
    EngineServices services_owner(false, custom_clock ? EngineServices::Now([&] {
                                      if (std::this_thread::get_id() != owner)
                                          foreign_clock = true;
                                      return 100.;
                                  })
                                                      : EngineServices::Now{});
    WorldContext context(WorldRole::Runtime, services_owner.access());
    Scene scene(context);
    auto& world = context.world();
    const auto services = context.services();
    std::vector<flecs::entity> entities;
    for (unsigned i = 0; i < 256; ++i)
        entities.push_back(world.entity().set<TraceCounter>({}));
    {
        RuntimeSimulation simulation(context, scene, {4});
        bool cleared = false; // Read/written only by the caller stage below.
        auto update =
            world.system<TraceCounter>("project.WorkerTraceUpdate")
                .kind(world.lookup("forge.runtime.Gameplay"))
                .multi_threaded()
                .each([mode, owner, services, &cleared](TraceCounter& value) {
                    ++value.updates;
                    if (mode == ProbeMode::Unmatched && value.updates == 2)
                        ecs_os_api.perf_trace_push_("worker fixture", 1, "Unfinished");
                    if (mode == ProbeMode::ClearDuringTick && value.updates == 2 &&
                        std::this_thread::get_id() == owner && !cleared) {
                        cleared = true;
                        services.clear_profiles();
                    }
                    if (mode == ProbeMode::Capacity && value.updates == 2 &&
                        std::this_thread::get_id() != owner) {
                        static thread_local bool emitted = false;
                        if (!emitted) {
                            emitted = true;
                            for (unsigned i = 0; i < profile_record_capacity + 8; ++i) {
                                ecs_os_api.perf_trace_push_("worker capacity", 1, "Burst");
                                ecs_os_api.perf_trace_pop_("worker capacity", 1, "Burst");
                            }
                        }
                    }
                });
        update.add<FixedSimulation>();
        services.profiling(true);
        simulation.tick(1.f / 60);
        for (const auto& row : services.profiles())
            check(!row.contains("native_stage"), "Disabled native capture emitted worker spans");
        if (tracing)
            services.profiling_systems(true);
        (void)simulation.system_profile();
        for (unsigned tick : {2u, 3u}) {
            services.clear_profiles();
            simulation.tick(1.f / 60);
            std::set<unsigned> stages;
            for (const auto& row : services.profiles()) {
                if (!row.contains("native_stage"))
                    continue;
                check(row.at("category") == "ecs.system" && row.at("tick") == tick,
                      "Worker span lost its owner capture/tick");
                check(
                    row.at("name").get<std::string>().find(
                        mode == ProbeMode::Capacity && tick == 2 ? "Burst" : "WorkerTraceUpdate") !=
                        std::string::npos,
                    "Worker attributed a serial system to a native stage");
                check(row.at("start_seconds").is_number() &&
                          std::isfinite(row.at("start_seconds").get<double>()) &&
                          row.at("seconds").get<double>() >= 0,
                      "Worker span has no real measured interval");
                stages.insert(row.at("native_stage").get<unsigned>());
            }
            const bool worker_records = tracing && !custom_clock &&
                                        !(tick == 2 && (mode == ProbeMode::Unmatched ||
                                                        mode == ProbeMode::ClearDuringTick));
            const bool clipped = mode == ProbeMode::Capacity && tick == 2;
            check(stages == (clipped          ? std::set<unsigned>{3}
                             : worker_records ? std::set<unsigned>{1, 2, 3}
                                              : std::set<unsigned>{}),
                  "Real native worker stage coverage/fallback is incorrect");
            const auto plan = simulation.system_profile();
            check(plan.at("system_trace_scope") ==
                      (tracing && !custom_clock ? "native_stages" : "owner_thread"),
                  "Native plan misstated supported worker capture");
            const auto timeline = cpu_scope_timeline(services.profile_status().at("samples"));
            const auto exported = cpu_scope_trace(services.profile_status(), "Worker fixture",
                                                  "2026-11-04 00:00:00 +0000");
            if (worker_records) {
                check(timeline.native_workers && timeline.measured_threads == (clipped ? 2 : 4) &&
                          exported.at("otherData").at("native_stages") ==
                              (clipped ? Json::array({0, 3}) : Json::array({0, 1, 2, 3})),
                      "Actual measured worker records lost their separate trace tracks");
                for (const auto& span : timeline.spans)
                    check(timeline.lane_stages.at(span.lane) == span.native_stage,
                          "Timeline assigned a worker interval to another stage");
            } else
                check(!timeline.native_workers &&
                          !exported.at("otherData").contains("native_stages"),
                      "Owner-only fallback fabricated worker trace metadata");
            if (mode == ProbeMode::Unmatched && tick == 2)
                check(services.profile_status().at("native_systems_dropped") > 0,
                      "Unfinished overflowing worker nesting was not accounted");
            if (clipped)
                check(services.profile_status().at("native_systems_dropped") == 27 &&
                          services.profiles().size() == profile_record_capacity,
                      "Per-stage overflow/global eviction accounting is incorrect");
        }
        const auto retained = services.profiles();
        services.profiling(false);
        simulation.tick(1.f / 60);
        check(services.profiles() == retained,
              "Stopped collection changed retained worker records");
        for (auto entity : entities)
            check(entity.get<TraceCounter>().updates == 4,
                  "Worker tracing changed numeric gameplay");
        check(!foreign_clock, "Worker called the owner/custom profiling clock");
        update.destruct();
    }
    check(world.get_threads() == 1, "Worker tracing interfered with native retirement");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--locked-api") {
            ecs_os_set_api_defaults();
            auto api = ecs_os_get_api();
            ecs_os_set_api(&api);
            check(!detail::initialize_native_system_tracing() &&
                      ecs_os_api.thread_new_ == api.thread_new_ &&
                      ecs_os_api.perf_trace_push_ == api.perf_trace_push_,
                  "Tracing changed an already selected external OS API");
            exercise(false, false);
        } else {
            check(argc == 1 && detail::initialize_native_system_tracing(),
                  "Native trace bootstrap failed");
            exercise(false, true);
            exercise(true, true);
            exercise(false, true, ProbeMode::Unmatched);
            exercise(false, true, ProbeMode::Capacity);
            exercise(false, true, ProbeMode::ClearDuringTick);
            test::worker_trace_concurrent_worlds<TraceCounter>(check);
            test::worker_trace_unwind(check);
        }
        std::cout << "Real worker spans, epochs/Stop, numeric preservation, clock/OS fallback and "
                     "retirement passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
