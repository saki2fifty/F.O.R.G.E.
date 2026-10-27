#pragma once
inline void scope_trace_tests() {
    double now = 10;
    unsigned reads = 0;
    EngineServices engine(false, [&] {
        ++reads;
        return now;
    });
    const auto services = engine.access();
    services.record_profile_span("host", "off", 5, .25);
    check(reads == 0 && services.profiles().empty(),
          "Disabled explicit span read a clock/recorded");
    for (double bad : {-1., 3601., std::numeric_limits<double>::infinity()})
        reject([&] { services.record_profile_span("host", "bad", 1, bad); });
    reject([&] {
        services.record_profile_span("host", "bad", std::numeric_limits<double>::quiet_NaN(), 1);
    });
    services.profiling(true);
#ifndef FORGE_DISABLE_PROFILING
    {
        auto outer = services.profile("host", "outer", 7);
        now = 11;
        {
            auto inner = services.profile("host", "inner", 7);
            now = 12;
        }
        now = 13;
    }
    services.record_profile("sdk", "duration only", .5, 7);
    services.record_profile_span("frame", "observed", 14, .25, 8);
    const auto source = services.profile_status();
    check(source.at("epoch") == 0 && reads == 4, "Span collection added clock reads or lost epoch");
    const auto timeline = cpu_scope_timeline(source.at("samples"));
    check(timeline.spans.size() == 3 && timeline.duration_only == 1 && timeline.lanes == 1,
          "Measured nested spans mixed with duration-only records");
    check(timeline.origin == 10 && timeline.spans[0].name == "outer" &&
              timeline.spans[0].start == 0 && timeline.spans[0].seconds == 3 &&
              timeline.spans[1].start == 1 && timeline.spans[1].seconds == 1,
          "Timeline lost real starts/order/duration");
    const auto text = cpu_scope_trace_text(source, "Test owner", "2026-10-27 00:00:00 +0000");
    const auto document = Json::parse(text);
    check(document.at("otherData").at("duration_only_records") == 1 &&
              document.at("otherData").at("owner_threads") == 1 &&
              document.at("otherData").at("source").at("epoch") == "0",
          "Trace metadata lost capture/owner provenance");
    unsigned complete = 0;
    for (const auto& event : document.at("traceEvents"))
        if (event.at("ph") == "X") {
            ++complete;
            if (event.at("name") == "inner")
                check(event.at("ts") == 1000000 && event.at("dur") == 1000000 &&
                          event.at("args").at("tick") == "7",
                      "Trace units are not relative microseconds");
        }
    check(complete == 3, "Trace fabricated SDK interval or dropped a measured scope");
    const auto page = cpu_scope_trace(source, "Owner", "time", 1, 1);
    check(page.at("otherData").at("exported_spans") == 1 &&
              page.at("otherData").at("measured_spans") == 3 &&
              page.at("traceEvents").back().at("ts") == 1000000,
          "Trace page reset its source origin or lost total/count");
    reject([&] { cpu_scope_trace(source, "Owner", "time", 0, 0); });
    reject([&] { cpu_scope_trace(source, "Owner", "time", 513, 1); });
    services.clear_profiles();
    check(services.profile_status().at("epoch") == 1 &&
              cpu_scope_timeline(services.profiles()).spans.empty(),
          "Clear retained timeline data");
    bool refused = false;
    std::thread worker([&] {
        try {
            services.record_profile_span("host", "wrong thread", 1, 1);
        } catch (const std::exception&) {
            refused = true;
        }
    });
    worker.join();
    check(refused && services.profiles().empty(), "Span access escaped the owner thread");
#else
    services.record_profile_span("host", "compiled out", 1, .5);
    check(reads == 0 && services.profiles().empty(),
          "Compile-out retained intervals or read clock");
#endif
    const auto sample = [](const char* name, double start, double seconds, std::uint64_t sequence) {
        return Json{{"category", "layout"},
                    {"name", name},
                    {"start_seconds", start},
                    {"seconds", seconds},
                    {"count", 1},
                    {"sequence", sequence},
                    {"tick", 4}};
    };
    Json crossed = Json::array({sample("inner", 2, 2, 1), sample("outer", 1, 5, 2),
                                sample("crossing", 3, 5, 3), sample("later", 8, 1, 4)});
    const auto lanes = cpu_scope_timeline(crossed);
    check(lanes.spans.size() == 4 && lanes.lanes == 2, "Crossing intervals were falsely nested");
    for (const auto& a : lanes.spans)
        for (const auto& b : lanes.spans) {
            if (a.sequence >= b.sequence || a.lane != b.lane)
                continue;
            const bool disjoint = a.start + a.seconds <= b.start || b.start + b.seconds <= a.start;
            const bool nested =
                (a.start <= b.start && a.start + a.seconds >= b.start + b.seconds) ||
                (b.start <= a.start && b.start + b.seconds >= a.start + a.seconds);
            check(disjoint || nested, "Trace layout lane violates complete-event nesting");
        }
    const auto zero = cpu_scope_timeline(Json::array({sample("point", -100, 0, 1)}));
    check(zero.spans.size() == 1 && zero.spans[0].start == 0 && zero.seconds == 0,
          "Zero-duration or negative clock origin was fabricated/dropped");
    for (const Json value : {Json(-1), Json(1.5), Json("bad")}) {
        auto invalid = crossed;
        invalid[0]["sequence"] = value;
        reject([&] { cpu_scope_timeline(invalid); });
    }
    for (const char* field : {"tick", "count", "seconds", "name", "start_seconds"}) {
        auto invalid = crossed;
        invalid[0][field] = Json::object();
        reject([&] { cpu_scope_timeline(invalid); });
    }
    auto duplicate = crossed;
    duplicate[1]["sequence"] = duplicate[0]["sequence"];
    reject([&] { cpu_scope_timeline(duplicate); });
    auto huge = crossed;
    huge[0]["start_seconds"] = 1e300;
    reject([&] { cpu_scope_timeline(huge); });
    auto gpu = crossed;
    gpu[0]["clock"] = "gpu";
    reject([&] { cpu_scope_timeline(gpu); });
    Json full = Json::array();
    for (std::size_t i = 0; i < profile_record_capacity; ++i)
        full.push_back(sample("bounded", double(i), 1000, i + 1));
    check(cpu_scope_timeline(full).lanes == profile_record_capacity,
          "Worst crossing layout exceeded/lost native bound");
    full.push_back(sample("overflow", 0, 1, 600));
    reject([&] { cpu_scope_timeline(full); });
    const Json escaped{{"samples", Json::array({sample("quote\"\\\n", 0, .001, 1)})},
                       {"epoch", 3},
                       {"sequence", 1}};
    const auto roundtrip =
        Json::parse(cpu_scope_trace_text(escaped, "Owner", "2026-10-27 00:00:00 +0000"));
    check(roundtrip.at("traceEvents").back().at("name") == "quote\"\\\n",
          "Trace JSON string escaping changed names");
    auto invalid_status = escaped;
    invalid_status["epoch"] = -1;
    reject([&] { cpu_scope_trace(invalid_status, "Owner", "time"); });
}
