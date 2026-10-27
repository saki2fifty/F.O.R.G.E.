#pragma once
void native_bridge_tests() {
    unsigned reads = 0;
    EngineServices engine(false, [&] { return double(++reads) * .001; });
    const auto services = engine.access();
    services.profiling_systems(true);
    const auto push = [](const char* name) { ecs_os_api.perf_trace_push_("test", 1, name); };
    const auto pop = [] { ecs_os_api.perf_trace_pop_("test", 1, nullptr); };
    {
        detail::NativeSystemTraceCapture capture(services, 9);
        push("disabled");
        pop();
    }
    check(reads == 0 && services.profiles().empty(), "Disabled native tracing read its clock");
    services.profiling(true);
#ifndef FORGE_DISABLE_PROFILING
    {
        detail::NativeSystemTraceCapture capture(services, 9);
        push("outer");
        push("inner");
        pop();
        pop();
    }
    auto spans = cpu_scope_timeline(services.profiles());
    check(reads == 4 && spans.spans.size() == 2 && spans.spans[0].name == "outer" &&
              spans.spans[0].tick == 9 && spans.spans[0].seconds > spans.spans[1].seconds,
          "Native callback bridge lost measured nesting/tick correlation");
    services.clear_profiles();
    const auto before_unmatched = reads;
    {
        detail::NativeSystemTraceCapture capture(services, 10);
        push("incomplete");
    }
    check(services.profiles().empty() && reads == before_unmatched + 1,
          "Unmatched native dispatch was recorded as completed");
    {
        auto cancelled = services.profile("host", "cancelled");
        cancelled.cancel();
    }
    check(services.profiles().empty(), "Cancelled host scope emitted a sample");
    {
        detail::NativeSystemTraceCapture capture(services, 10);
        push("owner");
        EngineServices other;
        {
            detail::NativeSystemTraceCapture disabled(other.access(), 0);
            push("other world disabled");
            pop();
        }
        const auto before_worker = reads;
        std::thread worker([&] {
            push("foreign thread");
            pop();
        });
        worker.join();
        check(reads == before_worker, "Foreign native callback accessed owner profiling");
        pop();
    }
    check(services.profiles().size() == 1 && services.profiles()[0].at("name") == "owner",
          "Nested disabled context lost or contaminated its previous owner");
    services.clear_profiles();
    {
        detail::NativeSystemTraceCapture capture(services, 11);
        for (unsigned i = 0; i < profile_system_depth_capacity + 16; ++i)
            push("deep");
        for (unsigned i = 0; i < profile_system_depth_capacity + 16; ++i)
            pop();
    }
    check(services.profiles().size() == profile_system_depth_capacity &&
              services.profile_status().at("native_systems_dropped") == 16,
          "Native depth bound corrupted nesting or hid dropped dispatches");
    const auto trace = cpu_scope_trace(services.profile_status(), "Owner", "time");
    check(trace.at("otherData").at("source").at("native_systems_dropped") == "16",
          "Copied trace lost native depth-loss metadata");
    services.clear_profiles();
    {
        detail::NativeSystemTraceCapture capture(services, 12);
        for (unsigned i = 0; i < profile_system_depth_capacity + 2; ++i)
            push("cleared");
        services.clear_profiles();
        for (unsigned i = 0; i < profile_system_depth_capacity + 2; ++i)
            pop();
    }
    check(services.profiles().empty() &&
              services.profile_status().at("native_systems_dropped") == 0,
          "An old native capture crossed Clear's record/loss epoch");
    {
        detail::NativeSystemTraceCapture capture(services, 13);
        const auto long_name = std::string(124, 'a') + "\xe9\x9b\xaa" + std::string(100, 'b');
        push(long_name.c_str());
        pop();
        push("invalid \xff native text");
        pop();
    }
    check(services.profiles()[0].at("name").get<std::string>().size() <= 128 &&
              services.profiles()[0].at("name").get<std::string>().ends_with("..."),
          "Native profile path exceeded its display bound");
    check(!Json(services.profiles()).dump().empty(), "Native text broke strict JSON output");
    services.profiling_systems(false);
    const auto retained = services.profiles();
    {
        detail::NativeSystemTraceCapture capture(services, 14);
        push("off");
        pop();
    }
    check(services.profiles() == retained, "Native opt-out erased or added samples");
#else
    {
        detail::NativeSystemTraceCapture capture(services, 9);
        push("compiled out");
        pop();
    }
    check(reads == 0 && services.profiles().empty() && !services.profiling_systems_enabled() &&
              !services.profiling_systems_available(),
          "Compiled-out native trace retained records or read a clock");
#endif
}
void native_dispatch_tests() {
    EngineContext engine(WorldRole::Runtime);
    Scene scene(engine.world());
    scene.reset({{"version", 1}, {"entities", Json::array()}});
    RuntimeSimulation runtime(engine.world(), scene);
    auto& world = engine.world().world();
    std::vector<char> callbacks;
    auto nested =
        world.system("project.TraceInner").run([&](flecs::iter&) { callbacks.push_back('I'); });
    world.system("project.TraceA")
        .kind(world.lookup("forge.runtime.Gameplay"))
        .run([&](flecs::iter&) {
            callbacks.push_back('A');
            ecs_run(world, nested.id(), 1.f / 60, nullptr);
        })
        .add<FixedSimulation>();
    auto skipped = world.system("project.TraceSkip")
                       .kind(world.lookup("forge.runtime.Gameplay"))
                       .interval(1000.f)
                       .run([&](flecs::iter&) { callbacks.push_back('S'); })
                       .add<FixedSimulation>();
    auto services = engine.services();
    services.profiling(true);
    services.profiling_systems(true);
    const auto authored = scene.snapshot();
    runtime.tick(1.f / 60);
    check(callbacks == std::vector<char>{'A', 'I'} && scene.snapshot() == authored,
          "Native trace changed actual callback/timer/source behavior");
#ifndef FORGE_DISABLE_PROFILING
    const auto rows = services.profiles();
    std::uint64_t inner = 0, outer = 0;
    for (const auto& row : rows) {
        if (row.at("category") != "ecs.system")
            continue;
        const auto name = row.at("name").get<std::string>();
        check(!name.ends_with("TraceSkip"), "Timer-gated dispatch gained an invented scope");
        check(row.contains("start_seconds") && row.at("tick") == 1,
              "Actual dispatch lost measured start/current fixed tick");
        if (name.ends_with("TraceInner"))
            inner = row.at("sequence");
        if (name.ends_with("TraceA"))
            outer = row.at("sequence");
    }
    check(inner && outer && inner < outer,
          "Actual native nested dispatch scopes missing/order wrong");
    services.clear_profiles();
    ecs_set_interval(world, skipped.id(), .001f);
    callbacks.clear();
    runtime.tick(1.f / 60);
    bool found = false;
    for (const auto& row : services.profiles())
        found |= row.at("category") == "ecs.system" &&
                 row.at("name").get<std::string>().ends_with("TraceSkip");
    check(found && std::find(callbacks.begin(), callbacks.end(), 'S') != callbacks.end(),
          "A real timer-fired native dispatch was not traced");
#else
    check(services.profiles().empty(), "Compiled-out runtime traced native dispatch");
#endif
}
