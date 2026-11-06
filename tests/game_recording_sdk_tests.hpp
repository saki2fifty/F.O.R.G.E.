// Included within the existing SDK Play fixture namespace.
void game_recording_sdk_tests() {
    Fixture f;
    f.audio_enabled = true;
    f.recording_enabled = true;
    unsigned ticks = 0, controls = 0;
    EngineModule module;
    module.id = "game.recording_fixture";
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.controls = [&](ModuleContext&) { ++controls; };
    module.scene_ready = [&](ModuleContext& context) {
        auto system = context.world.system()
                          .kind(context.world.lookup("forge.runtime.Gameplay"))
                          .run([&](flecs::iter& it) {
                              check(std::abs(it.delta_time() - 1.f / 60) < 1e-8f,
                                    "Offline SDK recording changed fixed delta");
                              ++ticks;
                          });
        system.add<FixedSimulation>();
    };
    f.modules.push_back(std::move(module));
    std::string session;
    auto host = build_activated(f, session);
    std::uint64_t id = 3;
    auto wire = [&](std::string command, Json fields = Json::object()) {
        fields["protocol"] = 2;
        fields["session"] = session;
        fields["id"] = ++id;
        fields["command"] = command;
        return host->handle(fields, RuntimeClock::Clock::now());
    };
    const auto initial = wire("snapshot");
    const auto generation = initial.at("activation").at("generation");
    host->pump(RuntimeClock::Clock::now() + std::chrono::hours(1));
    check(ticks == 0 && controls == 0 && host->status().at("clock").at("tick") == 0 &&
              initial.at("recording").at("started") == false,
          "Wall-clock polling advanced a fresh offline run");
    for (const Json& fields : {Json{{"generation", generation}, {"fps", true}, {"frames", 3u}},
                               Json{{"generation", generation}, {"fps", .5}, {"frames", 3u}},
                               Json{{"generation", generation}, {"fps", 30}, {"frames", -1}},
                               Json{{"generation", generation}, {"fps", 30}, {"frames", 4097u}},
                               Json{{"fps", 30}, {"frames", 3u}}})
        check(!wire("record_begin", fields).value("ok", true),
              "Invalid offline SDK recording start was accepted");
    const auto begin =
        wire("record_begin", {{"generation", generation}, {"fps", 30000. / 1001}, {"frames", 3u}});
    check(begin.value("ok", false) && begin.at("recording").at("completed_frames") == 0 &&
              !begin.at("timing").at("paused").get<bool>() && ticks == 0 && controls == 0,
          "Offline SDK recording did not begin as a fresh running clock");
    for (const auto command : {"resume", "pause", "step", "time_scale", "ui", "property_tune"})
        check(!wire(command).value("ok", true), "Ordinary control mutated an offline recording");
    check(!wire("snapshot", {{"input_events", Json::array()}}).value("ok", true),
          "Offline recording admitted interactive input");
    check(!wire("record_frame", {{"generation", generation}, {"frame", 1u}}).value("ok", true) &&
              !wire("record_frame",
                    {{"generation", generation.get<std::uint64_t>() + 1}, {"frame", 0u}})
                   .value("ok", true),
          "Recording admitted an out-of-order or stale-world interval");
    std::size_t bytes = 0;
    for (unsigned frame = 0; frame < 3; ++frame) {
        const auto before = host->status().at("clock").at("tick");
        host->pump(RuntimeClock::Clock::now() + std::chrono::hours(2));
        check(host->status().at("clock").at("tick") == before,
              "Editor/transport polling advanced offline simulation");
        const auto response = wire("record_frame", {{"generation", generation}, {"frame", frame}});
        check(response.value("ok", false), "Offline SDK frame failed");
        const auto& output = response.at("recording_frame");
        const auto pcm = decode_base64(output.at("data").get<std::string>(), 192000);
        const auto count = frame == 1 ? 1601u : 1602u;
        check(output.at("frame") == frame && output.at("format") == "s16le-stereo-48000-v1" &&
                  output.at("start_audio_frame") == bytes / 4 &&
                  output.at("audio_frames") == count && pcm.size() == count * 4 &&
                  std::ranges::all_of(pcm, [](std::byte b) { return b == std::byte{}; }) &&
                  response.at("recording").at("completed_frames") == frame + 1,
              "Offline SDK PCM, media progress or sequential receipt differs");
        bytes += pcm.size();
    }
    const auto final = wire("snapshot");
    check(bytes == 4805 * 4 && ticks == 6 && controls > 0 && final.at("timing").at("tick") == 6 &&
              final.at("timing").at("dropped_ticks") == 0 &&
              final.at("recording").at("audio_frames") == 4805,
          "Offline SDK recording skipped fixed work or accumulated timing drift");
    check(!wire("record_frame", {{"generation", generation}, {"frame", 3u}}).value("ok", true) &&
              wire("snapshot").at("recording").at("error") == "",
          "Completed recording accepted extra work or became failed after a rejected read");
    Fixture ordinary;
    std::string ordinary_session;
    auto normal = build_activated(ordinary, ordinary_session);
    check(!send(*normal, 4, "record_begin",
                {{"session", ordinary_session}, {"generation", 1u}, {"fps", 30}, {"frames", 3u}})
               .value("ok", true),
          "Ordinary Play admitted a recording control without the launch profile");
    Fixture invalid;
    auto config = make_config(invalid);
    config.recording = true;
    bool refused = false;
    try {
        SdkPlayRuntime no_audio(std::move(config));
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "Recording launch accepted a missing offline mixer");
}
