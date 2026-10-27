#include "cpu_profile_control.hpp"
#include "runtime_entity_creation.hpp"
#include <forge/runtime.hpp>
#include <iostream>
#include <limits>
#include <map>
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
void gpu_service_tests() {
    unsigned clock_reads = 0;
    EngineServices owner(false, [&] {
        ++clock_reads;
        return 1.;
    });
    const auto service = owner.access(capability(Capability::Profiling));
    const auto epoch = service.profile_epoch();
    service.record_gpu_profile(epoch, "Scene", "Geometry", .002, 7);
    check(clock_reads == 0 && service.profiles().empty() &&
              service.profile_status().at("samples").empty(),
          "GPU recording read CPU clock or entered CPU transport");
#ifdef FORGE_DISABLE_PROFILING
    check(service.gpu_profiles().empty(), "Compile-out GPU recording retained samples");
#else
    const auto sample = service.gpu_profiles().at(0);
    check(sample.at("clock") == "gpu" && sample.at("frame") == 7 && sample.at("seconds") == .002 &&
              !sample.contains("tick") && sample.at("count") == 1,
          "GPU provenance changed");
    service.profiling(true);
    service.record_gpu_profile(epoch, "Scene", "Geometry", .003, 8);
    service.record_profile("work", "CPU", .001, 9);
    check(service.gpu_profiles().size() == 2 && service.profiles().size() == 1,
          "CPU collection transition invalidated independent GPU collection");
    for (unsigned i = 0; i < 600; ++i)
        if (i % 2)
            service.record_profile("work", "CPU", .001, i);
        else
            service.record_gpu_profile(epoch, "Game", "Geometry", .002, i + 1);
    check(service.profiles().size() == 256 && service.gpu_profiles().size() == 256 &&
              service.gpu_profiles().front().at("frame") == 89,
          "GPU and CPU did not share the existing 512-record bound");
#endif
    service.clear_profiles();
    service.record_gpu_profile(epoch, "Scene", "Old", .001, 10);
    check(service.gpu_profiles().empty() && service.profile_epoch() != epoch,
          "Clear accepted an unfinished GPU sample");
    for (double value : {-1., 3601., std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()})
        reject(
            [&] { service.record_gpu_profile(service.profile_epoch(), "Game", "bad", value, 1); });
    reject([&] { service.record_gpu_profile(epoch, "", "bad", 0, 1); });
    reject([&] { service.record_gpu_profile(epoch, "Game", std::string(129, 'x'), 0, 1); });
    reject([&] { service.record_gpu_profile(epoch, "Game", "bad", 0, 0); });
    reject([&] { owner.access(0).gpu_profiles(); });
    bool foreign_rejected = false;
    std::thread foreign([&] {
        try {
            service.record_gpu_profile(epoch, "Game", "bad", 0, 1);
        } catch (...) {
            foreign_rejected = true;
        }
    });
    foreign.join();
    check(foreign_rejected && clock_reads == 0,
          "GPU recording ignored owner thread or read CPU clock");
    ServiceAccess expired;
    {
        EngineServices retired;
        expired = retired.access();
    }
    reject([&] { expired.gpu_profiles(); });
    reject([&] { expired.record_gpu_profile(0, "Game", "old", 0, 1); });
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
void runtime_publication_workload() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    const auto asset = AssetId::generate();
    const auto root = PrefabMemberId::generate(), child = PrefabMemberId::generate();
    const Json source{
        {"format", "forge.prefab"},
        {"version", 3},
        {"asset_id", asset},
        {"revision", 1u},
        {"root", root},
        {"members",
         Json::array(
             {{{"id", root},
               {"name", "Root"},
               {"components", {{"forge.local_translation", {{"x", 1}, {"y", 0}, {"z", 0}}}}}},
              {{"id", child},
               {"parent", root},
               {"name", "Child"},
               {"components", {{"forge.local_translation", {{"x", 0}, {"y", 2}, {"z", 0}}}}}}})}};
    scene.set_prefab_sources({{asset, source}});
    RuntimeSimulation runtime(engine.world(), scene);
    const auto token = detail::request_runtime_prefab(engine.world(), "project.profile", {}, asset,
                                                      "Measured group");
    engine.services().profiling(true);
    engine.services().clear_profiles();
    runtime.tick(1.f / 60);
    const auto result = detail::inspect_runtime_entity(engine.world(), "project.profile", token);
    check(result.state == detail::RuntimeEntityState::Ready && scene.entity_count() == 2 &&
              !scene.can_undo(),
          "Profiling changed Prefab publication or pose capture");
    const auto nodes = engine.world().transform_nodes();
    const auto document = scene.document();
    for (const auto& item : document.at("entities"))
        check(nodes.contains(scene.entity(item.at("id")).id()),
              "Published Prefab member was absent from presentation pose inputs");
    const auto samples = engine.services().profiles();
#ifndef FORGE_DISABLE_PROFILING
    std::map<std::string, std::size_t> stages;
    for (std::size_t i = 0; i < samples.size(); ++i)
        if (samples[i].at("category") == "runtime" && samples[i].at("tick") == 1)
            stages.emplace(samples[i].at("name").get<std::string>(), i);
    for (const auto* name :
         {"RuntimeEntityPublication", "PresentationPoseCapture", "FixedSimulationTick"})
        check(stages.contains(name), "Runtime publication/pose CPU stage was not recorded");
    const auto publication = stages.at("RuntimeEntityPublication");
    const auto capture = stages.at("PresentationPoseCapture");
    const auto fixed = stages.at("FixedSimulationTick");
    check(publication < capture && capture < fixed &&
              samples[publication].at("seconds") <= samples[fixed].at("seconds") &&
              samples[capture].at("seconds") <= samples[fixed].at("seconds"),
          "Runtime CPU scopes lost fixed-tick order or inclusive duration");
    if (engine.services().available(Capability::Resources)) {
        check(stages.contains("ResourceSynchronization"), "Resource CPU stage was not recorded");
        const auto resources = stages.at("ResourceSynchronization");
        check(publication < resources && resources < capture &&
                  samples[resources].at("seconds") <= samples[fixed].at("seconds"),
              "Resource synchronization profiling changed stage order");
    } else
        check(!stages.contains("ResourceSynchronization"), "Absent resources produced a CPU scope");
#else
    check(samples.empty(), "Compile-out runtime retained CPU scopes");
#endif
    engine.services().profiling(false);
    runtime.tick(1.f / 60);
    check(engine.services().profiles() == samples, "Stopped runtime collection changed samples");
    detail::release_runtime_entity(engine.world(), "project.profile", token);
}
void native_system_profiles() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.reset({{"version", 1}, {"entities", Json::array()}});
    RuntimeSimulation runtime(engine.world(), scene);
    auto& world = scene.world();
    std::vector<ecs_entity_t> executed;
    const auto system = [&](const char* name, const char* phase) {
        return world.system(name).kind(world.lookup(phase)).run([&](flecs::iter& it) {
            executed.push_back(it.system().id());
        });
    };
    auto first = system("ProbeFirst", "forge.runtime.Gameplay").add<FixedSimulation>();
    auto second = system("ProbeSecond", "forge.runtime.PostAnimation").add<FixedSimulation>();
    auto excluded = system("NotFixed", "forge.runtime.Gameplay");
    struct Unmatched {
        int value;
    };
    world.system<Unmatched>("UnmatchedSystem")
        .kind(world.lookup("forge.runtime.Gameplay"))
        .each([](Unmatched&) {})
        .add<FixedSimulation>();
    check(!runtime.system_profile().at("available").get<bool>() && executed.empty(),
          "Inspection executed gameplay before a real tick");
    runtime.tick(1.f / 60);
    check(!runtime.system_profile().at("available").get<bool>(),
          "Stopped collection captured a native plan");
    engine.services().profiling(true);
    executed.clear();
    runtime.tick(1.f / 60);
#ifndef FORGE_DISABLE_PROFILING
    const auto profile = runtime.system_profile();
    check(profile.at("available") && profile.at("tick") == 2 &&
              profile.at("timing") == "native_system_lifetime" && profile.at("truncated") == false,
          "Real fixed tick did not publish native plan");
    std::vector<ecs_entity_t> observed;
    unsigned merges = 0;
    for (const auto& row : profile.at("entries")) {
        if (row.at("kind") == "merge") {
            ++merges;
            continue;
        }
        const auto id = row.at("id").get<ecs_entity_t>();
        check(id != excluded.id() && row.at("name") != "::UnmatchedSystem",
              "Native plan included an unselected or inactive system");
        if (id == first.id() || id == second.id()) {
            observed.push_back(id);
            check(row.at("cpu_seconds") == ecs_system_get(world, id)->time_spent &&
                      !row.at("immediate").get<bool>() && !row.at("multi_threaded").get<bool>(),
                  "Copied CPU total/mode disagrees with actual native system");
        }
    }
    check(observed == executed && observed == std::vector<ecs_entity_t>{first, second} && merges,
          "Native plan order disagrees with actual gameplay invocation order");
    check(runtime.system_profile() == profile,
          "Repeated inspection resampled or advanced gameplay");
    runtime.tick(1.f / 60);
    runtime.tick(1.f / 60);
    const auto requested = runtime.system_profile();
    check(requested.at("tick") == 3, "Unwatched collection repeatedly captured the native plan");
    engine.services().profiling(false);
    runtime.tick(1.f / 60);
    check(runtime.system_profile() == requested, "Stop changed retained native plan");
    const auto native_seconds = ecs_system_get(world, first)->time_spent;
    engine.services().clear_profiles();
    check(!runtime.system_profile().at("available").get<bool>() &&
              ecs_system_get(world, first)->time_spent == native_seconds,
          "Clear retained old plan or reset native lifetime counters");
    engine.services().profiling(true);
    first.disable();
    executed.clear();
    runtime.tick(1.f / 60);
    for (const auto& row : runtime.system_profile().at("entries"))
        check(row.value("id", ecs_entity_t{}) != first.id(),
              "Disabled system survived native rebuild");
    check(executed == std::vector<ecs_entity_t>{second}, "Disabled system still ran");
    first.enable();
    const auto long_name = std::string(156, 'x') + "\xe2\x98\x83" + std::string(100, 'x');
    auto long_system = system(long_name.c_str(), "forge.runtime.Gameplay").add<FixedSimulation>();
    for (int i = 0; i < 300; ++i)
        system(("Bound" + std::to_string(i)).c_str(), "forge.runtime.Gameplay")
            .add<FixedSimulation>();
    runtime.tick(1.f / 60);
    const auto bounded = runtime.system_profile();
    check(bounded.at("truncated") && bounded.at("entries").size() == 256 &&
              bounded.at("total_entries") > 300 && bounded.dump().size() < 1024 * 1024,
          "Native plan exceeded copied observation bound or hid truncation");
    bool found_long = false;
    for (const auto& row : bounded.at("entries"))
        if (row.value("id", ecs_entity_t{}) == long_system.id()) {
            found_long = true;
            check(row.at("name").get<std::string>().size() <= 160 &&
                      row.at("name").get<std::string>().ends_with("..."),
                  "Native display name exceeded bound");
        }
    check(found_long, "Long-name fixture was outside inspected native plan");
    bool rejected = false;
    std::thread other([&] {
        try {
            (void)runtime.system_profile();
        } catch (const std::exception&) {
            rejected = true;
        }
    });
    other.join();
    check(rejected, "Non-owner thread inspected native plan");
    EngineContext replacement(WorldRole::Runtime);
    Scene replacement_scene(replacement.world());
    RuntimeSimulation replacement_runtime(replacement.world(), replacement_scene);
    check(!replacement_runtime.system_profile().at("available").get<bool>(),
          "Fresh world retained old native plan");
#else
    check(!runtime.system_profile().at("available").get<bool>(),
          "Compile-out collection captured native plan");
#endif
}
} // namespace
int main() {
    try {
        service_tests();
        gpu_service_tests();
        runtime_workload();
        runtime_publication_workload();
        native_system_profiles();
        std::cout << "CPU profiling tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
