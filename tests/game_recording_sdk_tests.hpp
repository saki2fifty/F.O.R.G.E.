// Included within the existing SDK Play fixture namespace.
void movie_scene_response_budget() {
    Fixture calibration;
    std::string calibration_session;
    auto calibration_host = build_activated(calibration, calibration_session);
    const auto overhead =
        send(*calibration_host, 4u, "snapshot", {{"session", calibration_session}}).dump().size();
    constexpr std::size_t limit = 16u * 1024u * 1024u;
    constexpr std::size_t headroom = 128u * 1024u;
    check(overhead + headroom < limit, "Budget fixture schema already exceeds the wire limit");
    // The unknown text appears in both authored and effective Scene snapshots.
    // Leave less than a full 1 FPS PCM reply, accounting for the real schema.
    const auto text_bytes = (limit - overhead - headroom) / 2;
    for (const bool recording : {false, true}) {
        Fixture f;
        f.audio_enabled = f.recording_enabled = recording;
        WorldContext world;
        Scene big(world);
        big.replace(
            {{"version", 1},
             {"entities",
              Json::array({{{"id", "large"},
                            {"name", "Large"},
                            {"components",
                             {{"missing.plugin", {{"text", std::string(text_bytes, 'b')}}}}}}})}});
        std::filesystem::create_directories(f.project_root / "Scenes");
        big.save(f.project_root / "Scenes/large.scene.json");
        AssetCatalog catalog(f.project_root);
        catalog.add_scene("Scenes/large.scene.json");
        catalog.save(AssetCatalog::project_index(f.project_root));
        std::string session;
        auto host = build_activated(f, session);
        std::uint64_t id = 3, epoch = 1;
        const auto wire = [&](const std::string& command, Json fields = Json::object()) {
            fields["session"] = session;
            return send(*host, ++id, command, fields);
        };
        auto state =
            wire("snapshot",
                 {{"editor_epoch",
                   {{"epoch", epoch}, {"captured", false}, {"input_device", "EditorHost"}}}});
        const auto generation = state.at("activation").at("generation");
        const auto original = state.at("scene");
        const auto acknowledge = [&] {
            auto acks = Json::array();
            for (auto effect : host->live_effects()) {
                effect["session"] = session;
                effect["accepted"] = true;
                acks.push_back(std::move(effect));
            }
            if (!acks.empty())
                check(wire("snapshot", {{"platform_acks", acks}}).value("ok", false),
                      "Budget fixture failed to acknowledge its released cursor");
            if (wire("snapshot").value("release_required", false))
                check(wire("snapshot", {{"editor_epoch",
                                         {{"epoch", ++epoch},
                                          {"captured", false},
                                          {"input_device", "EditorHost"}}}})
                          .value("ok", false),
                      "Budget fixture failed its release observation");
        };
        if (recording)
            check(wire("record_begin", {{"generation", generation},
                                        {"fps", 1},
                                        {"frames", 1u},
                                        {"capture_scope", "session"}})
                      .value("ok", false),
                  "Movie transport-budget fixture could not begin");
        f.capture.service->request(
            "game.test", {{"operation", "prepare"}, {"asset", big.asset_id()}, {"activate", true}});
        if (recording) {
            Json response;
            std::uint64_t before = 0;
            for (unsigned poll = 0; poll < 8; ++poll) {
                before = wire("snapshot").at("recording").at("audio_frames").get<std::uint64_t>();
                response = wire("record_frame", {{"generation", generation}, {"frame", 0u}});
                if (!response.value("ok", true))
                    break;
                check(!response.contains("recording_frame"),
                      "Budget fixture advanced past its pending interval");
                acknowledge();
            }
            check(!response.is_null() && !response.value("ok", true),
                  "Movie admitted a world without reserving its PCM reply");
            state = wire("snapshot");
            check(!host->pending_candidate() &&
                      state.at("activation").at("generation") == generation &&
                      state.at("scene") == original &&
                      state.at("recording").at("audio_frames").get<std::uint64_t>() == before &&
                      host->loading().value("state", "") == "failed" &&
                      host->loading().value("error", "").find("projected post-activation") !=
                          std::string::npos,
                  "Audio-budget rejection published a candidate, consumed PCM or replaced the old "
                  "world");
        } else {
            for (unsigned poll = 0; poll < 8 && !host->pending_candidate() &&
                                    host->loading().value("state", "") != "failed";
                 ++poll) {
                host->pump(RuntimeClock::Clock::now());
                acknowledge();
            }
            check(host->pending_candidate().has_value(),
                  "The same world should fit ordinary Play without a PCM interval");
        }
    }
}
void session_game_recording_sdk_tests() {
    for (unsigned mode = 0; mode < 3; ++mode) {
        const bool replay = mode == 1, preload = mode == 2;
        Fixture f;
        f.audio_enabled = f.recording_enabled = true;
        AssetId first_scene, second_scene;
        unsigned ticks = 0;
        EngineModule module;
        module.id = "test.movie_visits";
        module.dependencies = {"forge.game"};
        module.required_services = module.allowed_services = capability(Capability::Game);
        module.runtime_roles = role_mask(WorldRole::Runtime);
        module.scene_ready = [&](ModuleContext& context) {
            const auto controls = context.services.game();
            auto prepared = std::make_shared<std::uint64_t>(0);
            context.world.system()
                .kind(context.world.lookup("forge.runtime.Gameplay"))
                .run([&, controls, prepared](flecs::iter&) {
                    ++ticks;
                    if (preload) {
                        if (ticks == 2)
                            *prepared =
                                controls->request("test.movie_visits", {{"operation", "prepare"},
                                                                        {"asset", second_scene},
                                                                        {"activate", false}});
                        if (*prepared &&
                            controls->query().at("preparation").value("ready", false)) {
                            const auto result = controls->inspect("test.movie_visits", *prepared);
                            check(result.state == "succeeded",
                                  "Movie preload request did not complete");
                            controls->request(
                                "test.movie_visits",
                                {{"operation", "activate"}, {"ticket", result.value.at("ticket")}});
                            controls->release("test.movie_visits", *prepared);
                            *prepared = 0;
                        }
                    } else if (ticks == 2 || ticks == 4)
                        controls->request("test.movie_visits",
                                          {{"operation", "prepare"},
                                           {"asset", ticks == 2 ? second_scene : first_scene},
                                           {"activate", true}});
                })
                .add<FixedSimulation>();
        };
        f.modules.push_back(std::move(module));
        std::string session;
        auto host = build_activated(f, session);
        std::uint64_t id = 3, epoch = 0;
        const auto wire = [&](const std::string& command, Json fields = Json::object()) {
            fields["session"] = session;
            auto response = send(*host, ++id, command, fields);
            if (command == "snapshot" && !response.value("ok", false))
                throw std::runtime_error("Session movie snapshot failed: " + response.dump());
            return response;
        };
        auto state = wire("snapshot");
        first_scene = state.at("scene").at("asset_id").get<AssetId>();
        WorldContext first_world, second_world;
        Scene a(first_world), b(second_world);
        a.restore_snapshot(state.at("scene"));
        b.replace({{"version", 1}, {"entities", Json::array()}});
        second_scene = b.asset_id();
        std::filesystem::create_directories(f.project_root / "Scenes");
        a.save(f.project_root / "Scenes/a.scene.json");
        b.save(f.project_root / "Scenes/b.scene.json");
        AssetCatalog catalog(f.project_root);
        catalog.add_scene("Scenes/a.scene.json");
        catalog.add_scene("Scenes/b.scene.json");
        catalog.save(AssetCatalog::project_index(f.project_root));
        Json begin{{"generation", state.at("activation").at("generation")},
                   {"fps", 1},
                   {"frames", 1u},
                   {"capture_scope", "session"}};
        auto bad = begin;
        bad["capture_scope"] = "unexpected";
        check(!wire("record_begin", bad).value("ok", true), "Movie admitted an unknown scope");
        RuntimeInput input;
        input.configure(InputMap{});
        input.begin_session_input_recording(
            input_recording_source(state.at("scene"), f.project_root), 60, first_scene);
        input.latch(1);
        input.latch(2);
        input.finish_input_recording();
        bad = begin;
        bad["input_recording_document"] = input.input_recording_document();
        bad["input_recording_document"]["source"] = "wrong";
        check(!wire("record_begin", bad).value("ok", true),
              "Movie admitted an incompatible replay source");
        state = wire("snapshot");
        check(state.at("timing").at("paused") == true && ticks == 0 &&
                  state.at("recording").at("started") == false &&
                  state.at("input").at("recording").at("mode") == "idle",
              "Rejected movie admission mutated input, clock or media state");
        if (replay)
            begin["input_recording_document"] = input.input_recording_document();
        state = wire("record_begin", begin);
        check(state.value("ok", false) &&
                  state.at("recording").at("profile") == "offline-session-game-v2" &&
                  state.at("recording").at("visits").size() == 1 && ticks == 0,
              "Session movie did not admit before its first tick");
        unsigned pending = 0, admissions = 0;
        std::uint64_t last_candidate = 0;
        while (!state.contains("recording_frame")) {
            check(++pending < 200, "Session movie did not finish a bounded interval");
            state = wire("record_frame",
                         {{"generation", state.at("activation").at("generation")}, {"frame", 0u}});
            if (!state.value("ok", false))
                throw std::runtime_error("Session movie interval failed: " + state.dump());
            if (state.contains("recording_frame"))
                break;
            const auto held = state.at("recording").at("audio_frames");
            const auto held_ticks = ticks;
            for (unsigned poll = 0; poll < 3; ++poll)
                host->pump(RuntimeClock::Clock::now() + std::chrono::hours(1));
            auto observed = wire("snapshot");
            check(observed.at("recording").at("audio_frames") == held && ticks == held_ticks,
                  "Wall polling advanced partial movie audio or fixed ticks");
            Json fields{
                {"editor_epoch",
                 {{"epoch", ++epoch}, {"captured", false}, {"input_device", "EditorHost"}}}};
            auto acks = Json::array();
            for (auto effect : host->live_effects()) {
                effect["session"] = session;
                effect["accepted"] = true;
                acks.push_back(std::move(effect));
            }
            if (!acks.empty())
                (void)wire("snapshot", {{"platform_acks", std::move(acks)}});
            if (host->pending_candidate()) {
                const auto ticket = host->pending_candidate()->at("ticket").get<std::uint64_t>();
                if (ticket != last_candidate) {
                    check(wire("candidate", {{"ticket", ticket}}).value("ok", false),
                          "Movie Scene candidate fetch failed");
                    fields["candidate_ack"] = {
                        {"session", session}, {"ticket", ticket}, {"accepted", true}};
                    last_candidate = ticket;
                    ++admissions;
                }
            }
            state = wire("snapshot", fields);
        }
        const auto& recording = state.at("recording");
        const auto& visits = recording.at("visits");
        const auto pcm =
            decode_base64(state.at("recording_frame").at("data").get<std::string>(), 192000);
        check(recording.at("completed_frames") == 1 && recording.at("audio_frames") == 48000 &&
                  pcm.size() == 192000 && admissions == (preload ? 1u : 2u) &&
                  visits.size() == (preload ? 2u : 3u) &&
                  visits.at(0).at("scene").get<AssetId>() == first_scene &&
                  visits.at(1).at("scene").get<AssetId>() == second_scene &&
                  visits.back().at("scene").get<AssetId>() ==
                      (preload ? second_scene : first_scene) &&
                  visits.at(1).at("audio_frame").get<unsigned>() > 0 &&
                  (preload || visits.at(2).at("audio_frame").get<unsigned>() >
                                  visits.at(1).at("audio_frame").get<unsigned>()),
              "Movie lost sequential PCM or repeated Scene visit/sample provenance");
        check(ticks >= 58 && ticks <= 60 && state.at("timing").at("dropped_ticks") == 0,
              "Scene preparation introduced phantom ticks or dropped simulation time");
        check(!wire("record_frame", {{"generation", begin.at("generation")}, {"frame", 0u}})
                   .value("ok", true),
              "Session movie admitted an old world/frame after activation");
        if (replay)
            check(state.at("input").at("recording").at("mode") == "finished",
                  "Session movie did not consume its atomically admitted replay");
    }
}
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
