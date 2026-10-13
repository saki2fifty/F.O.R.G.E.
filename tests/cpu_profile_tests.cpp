#include "cpu_profile_control.hpp"
#include <forge/runtime.hpp>
#include <iostream>
#include <limits>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
template <class F> void reject(F&& operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "Invalid profiling operation accepted");
}
void service_tests() {
    double now = 10;
    unsigned reads = 0;
    bool throw_clock = false;
    EngineServices owner(false, [&] {
        ++reads;
        if (throw_clock)
            throw std::runtime_error("Test clock failure");
        return now;
    });
    const auto service = owner.access(capability(Capability::Profiling));
    auto inactive = service.profile("test", "off");
    check(reads == 0 && service.profiles().empty(), "Disabled scope read clock or collected");
    service.profiling(true);
#ifndef FORGE_DISABLE_PROFILING
    {
        auto first = service.profile("work", "Outer", 42);
        now = 11;
        {
            auto nested = service.profile("work", "Inner", 42);
            now = 13;
        }
        now = 15;
    }
    const auto samples = service.profiles();
    check(samples.size() == 2 && samples[0].at("seconds") == 2 && samples[1].at("seconds") == 5 &&
              samples[0].at("tick") == 42 && samples[0].at("sequence") < samples[1].at("sequence"),
          "Nested scope oracle failed");
    for (int i = 0; i < 600; ++i)
        service.record_profile("work", "Explicit", .001, i);
    const auto bounded = service.profile_status();
    check(bounded.at("samples").size() == 512 && bounded.at("samples")[0].at("tick") == 88 &&
              bounded.at("samples").back().at("tick") == 599,
          "Ring eviction order failed");
    service.clear_profiles();
    {
        auto old = service.profile("work", "Old");
        service.clear_profiles();
        now += 1;
    }
    check(service.profiles().empty(), "Clear retained an unfinished scope");
    {
        auto old = service.profile("work", "Old");
        service.profiling(false);
        now += 100;
        service.profiling(true);
    }
    check(service.profiles().empty(), "Restart retained a scope from before Stop");
    {
        auto moved = service.profile("work", "Moved");
        auto final = std::move(moved);
        now += 2;
    }
    check(service.profiles().size() == 1 && service.profiles()[0].at("seconds") == 2,
          "Moved scope recorded incorrectly");
    service.clear_profiles();
    now = std::numeric_limits<double>::quiet_NaN();
    {
        auto invalid = service.profile("work", "InvalidStart");
    }
    now = 1;
    {
        auto invalid = service.profile("work", "InvalidEnd");
        now = std::numeric_limits<double>::infinity();
    }
    now = 1;
    {
        auto reversed = service.profile("work", "Reversed");
        now = 0;
    }
    now = 1;
    {
        auto overflow = service.profile("work", "TooLong");
        now = 3602;
    }
    throw_clock = true;
    {
        auto invalid = service.profile("work", "ThrowStart");
    }
    throw_clock = false;
    now = 1;
    {
        auto invalid = service.profile("work", "ThrowEnd");
        throw_clock = true;
    }
    throw_clock = false;
    {
        auto invalid = service.profile(nullptr, "InvalidName");
    }
    check(service.profiles().empty(), "Invalid instrumentation clock/name produced a record");
#else
    {
        auto omitted = service.profile("test", "compile-out");
    }
    service.record_profile("test", "compile-out", 1);
    check(reads == 0 && !service.profile_status().at("compiled").get<bool>() &&
              !service.profile_status().at("enabled").get<bool>() && service.profiles().empty(),
          "Compile-out collected data or read a clock");
#endif
    for (const double value : {-1., 3601., std::numeric_limits<double>::infinity()})
        reject([&] { service.record_profile("test", "bad", value); });
    reject([&] { service.record_profile("", "bad", 0); });
    reject([&] { owner.access(capability(Capability::Diagnostics)).clear_profiles(); });
    bool foreign_rejected = false;
    std::thread worker([&] {
        try {
            service.profile_status();
        } catch (...) {
            foreign_rejected = true;
        }
    });
    worker.join();
    check(foreign_rejected, "Foreign-thread profiling access succeeded");
    service.profiling(false);
    const auto before = reads;
    {
        auto disabled = service.profile("test", "off-again");
    }
    check(reads == before, "Disabled scope read clock after collection");
    const Json command{{"generation", 7}, {"enabled", true}, {"clear", true}};
    for (auto invalid :
         {Json{{"enabled", true}}, Json{{"generation", 8}, {"enabled", true}},
          Json{{"generation", 7.0}, {"enabled", true}}, Json{{"generation", 7}, {"enabled", 1}},
          Json{{"generation", 7}, {"enabled", true}, {"clear", 1}}}) {
        const auto snapshot = service.profile_status();
        reject([&] { control_cpu_profile(invalid, service, 7); });
        check(service.profile_status() == snapshot, "Rejected control mutated recording state");
    }
    check(control_cpu_profile(command, service, 7), "Valid generation control failed");
    reject([&] { cpu_profile_watch({{"profile_watch", 1}}); });
    check(!cpu_profile_watch(Json::object()) && cpu_profile_watch({{"profile_watch", true}}),
          "Optional profile watch policy failed");
    ServiceAccess expired;
    {
        EngineServices temporary;
        expired = temporary.access();
    }
    reject([&] { expired.profile_status(); });
}
void runtime_workload() {
    for (unsigned count : {0u, 256u, 1024u}) {
        EngineContext engine(WorldRole::Runtime);
        Scene scene(engine.world());
        Json source{{"version", 3}, {"asset_id", AssetId::generate()}, {"entities", Json::array()}};
        for (unsigned i = 0; i < count; ++i)
            source["entities"].push_back(
                {{"id", EntityId::generate()},
                 {"name", "Workload"},
                 {"components",
                  {{"forge.local_translation", {{"x", double(i)}, {"y", 0}, {"z", 0}}}}}});
        scene.reset(source);
        if (engine.world().transform_nodes().size() != count)
            throw std::runtime_error("Workload did not create the declared transform nodes");
        RuntimeSimulation runtime(engine.world(), scene);
        const auto authored = scene.document();
        engine.services().profiling(true);
        for (int i = 0; i < 120; ++i)
            runtime.tick(1.f / 60);
        const auto status = engine.services().profile_status();
        check(status.at("samples").size() <= 512 && scene.document() == authored,
              "Profiling changed authored scene or exceeded bound");
#ifndef FORGE_DISABLE_PROFILING
        unsigned ticks = 0;
        for (const auto& sample : status.at("samples"))
            if (sample.at("name") == "FixedSimulationTick") {
                ++ticks;
                check(sample.at("tick") >= 1 && sample.at("tick") <= 120 &&
                          sample.at("seconds").get<double>() >= 0,
                      "Measured fixed tick invalid");
            }
        check(ticks > 0, "Real runtime did not produce CPU tick samples");
#endif
        std::cout << "CPU workload entities=" << count
                  << " ticks=120 samples=" << status.at("samples").size() << '\n';
    }
}
} // namespace
int main() {
    try {
        service_tests();
        runtime_workload();
        std::cout << "CPU profiling tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
