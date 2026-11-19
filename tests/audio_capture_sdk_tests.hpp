// Included within the existing SDK Play fixture namespace.
void live_audio_capture_sdk_tests() {
    for (const auto output : {AudioOutput::Offline, AudioOutput::NullDeviceTest}) {
        Fixture f;
        f.audio_enabled = true;
        f.audio_output = output;
        std::string session;
        auto host = build_activated(f, session);
        std::uint64_t id = 3;
        auto now = RuntimeClock::Clock::now();
        const auto wire = [&](const std::string& command, Json fields = Json::object()) {
            fields["session"] = session;
            return send(*host, ++id, command, fields, now);
        };
        auto state = wire("snapshot",
                          {{"editor_epoch",
                            {{"epoch", 1u}, {"captured", false}, {"input_device", "EditorHost"}}}});
        const auto world = state.at("activation").at("generation");
        const auto initial_tick = host->status().at("clock").at("tick");
        auto response = wire("audio_capture", {{"operation", "begin"}, {"generation", world}});
        if (output == AudioOutput::Offline) {
            check(!response.value("ok", true) &&
                      host->status().at("clock").at("tick") == initial_tick &&
                      host->status().at("state") == "paused",
                  "Live capture admitted an offline mixer or altered its clock");
            continue;
        }
        check(response.value("ok", false) && !response.contains("scene"),
              "Live audio Begin failed or republished a complete world");
        const auto first_capture = response.at("audio_capture").at("generation");
        check(host->status().at("state") == "paused" &&
                  host->status().at("clock").at("tick") == initial_tick,
              "Live audio Begin resumed or rewound gameplay");
        check(!wire("audio_capture", {{"operation", "begin"}, {"generation", world}})
                   .value("ok", true),
              "A second Begin replaced an active capture");
        check(!wire("audio_capture", {{"operation", "read"},
                                      {"generation", world},
                                      {"capture_generation", 0u},
                                      {"audio_frame", 0u},
                                      {"maximum_frames", 4096u}})
                   .value("ok", true),
              "Live audio accepted a stale capture generation");
        check(wire("resume").value("ok", false), "Live capture blocked ordinary Resume");
        for (unsigned i = 0; i < 4; ++i) {
            auto acks = Json::array();
            for (auto effect : host->live_effects()) {
                effect["session"] = session;
                effect["accepted"] = true;
                acks.push_back(std::move(effect));
            }
            if (!acks.empty())
                (void)wire("snapshot", {{"platform_acks", acks}});
            now += std::chrono::milliseconds(25);
            host->pump(now);
        }
        check(host->status().at("clock").at("tick").get<std::uint64_t>() >
                  initial_tick.get<std::uint64_t>(),
              "Live audio capture held the ordinary gameplay clock");
        std::uint64_t cursor = 0;
        const auto read = [&](const Json& generation, const Json& capture) {
            auto reply = wire("audio_capture", {{"operation", "read"},
                                                {"generation", generation},
                                                {"capture_generation", capture},
                                                {"audio_frame", cursor},
                                                {"maximum_frames", 4096u}});
            check(reply.value("ok", false) && !reply.contains("scene"),
                  "Live audio read failed or republished its world");
            const auto& chunk = reply.at("audio_capture_chunk");
            const auto count = chunk.at("audio_frames").get<unsigned>();
            const auto bytes = decode_base64(chunk.at("data").get<std::string>(), 4096 * 4);
            check(chunk.at("format") == "s16le-stereo-48000-v1" &&
                      chunk.at("start_audio_frame") == cursor && count <= 4096 &&
                      bytes.size() == count * 4 &&
                      std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{}; }),
                  "Empty-world live PCM had an invalid sequential interval or nonzero samples");
            cursor += count;
            check(reply.at("audio_capture").at("read_frames") == cursor &&
                      reply.at("audio_capture").at("world_generation") == generation,
                  "Live audio progress drifted from its retained world/cursor");
            return reply;
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!cursor) {
            check(std::chrono::steady_clock::now() < deadline,
                  "SDK live capture did not receive device callbacks");
            response = read(world, first_capture);
            if (!cursor)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(!wire("audio_capture", {{"operation", "read"},
                                      {"generation", world},
                                      {"capture_generation", first_capture},
                                      {"audio_frame", cursor + 1},
                                      {"maximum_frames", 4096u}})
                   .value("ok", true),
              "Live audio accepted a skipped PCM cursor");
        check(!wire("audio_capture", {{"operation", "read"},
                                      {"generation", world},
                                      {"capture_generation", first_capture},
                                      {"audio_frame", cursor},
                                      {"maximum_frames", 4097u}})
                   .value("ok", true),
              "Live audio accepted an unbounded read");
        response = read(world, first_capture);
        check(wire("pause").value("ok", false), "Live capture blocked ordinary Pause");
        const auto tick = host->status().at("clock").at("tick").get<std::uint64_t>();
        check(wire("step").value("ok", false) &&
                  host->status().at("clock").at("tick").get<std::uint64_t>() == tick + 1,
              "Live capture blocked ordinary Step");
        check(wire("resume").value("ok", false), "Live capture blocked ordinary Resume");
        response = wire(
            "audio_capture",
            {{"operation", "end"}, {"generation", world}, {"capture_generation", first_capture}});
        check(response.value("ok", false) && host->status().at("state") == "running" &&
                  response.at("audio_capture").at("state") == "stopped",
              "Ending live audio capture stopped or paused ordinary Play");
        while (response.at("audio_capture").at("queued_frames") != 0)
            response = read(world, first_capture);
        check(response.at("audio_capture").at("captured_frames") == cursor,
              "Final live PCM prefix did not match its captured samples");
        response = wire("audio_capture", {{"operation", "begin"}, {"generation", world}});
        check(response.value("ok", false) &&
                  response.at("audio_capture").at("generation") != first_capture,
              "SDK live audio capture did not restart with a fresh generation");
        const auto second_capture = response.at("audio_capture").at("generation");
        check(!wire("audio_capture", {{"operation", "end"},
                                      {"generation", world},
                                      {"capture_generation", first_capture}})
                   .value("ok", true),
              "A stale End stopped a newer live capture");
        Json replacement{{"version", 1}, {"entities", Json::array()}};
        check(wire("replace", {{"scene", replacement}}).value("ok", false),
              "Live audio blocked ordinary Scene replacement");
        host->pump(now);
        const auto candidate = host->pending_candidate();
        check(candidate.has_value(), "Replacement candidate was unavailable during live capture");
        (void)wire("snapshot", {{"candidate_ack",
                                 {{"session", session},
                                  {"ticket", candidate->at("ticket")},
                                  {"accepted", true},
                                  {"diagnostic", ""}}}});
        host->pump(now);
        cursor = 0;
        response = read(world, second_capture);
        check(response.at("audio_capture").at("active_generation") != world &&
                  response.at("audio_capture").at("state") == "stopped",
              "Retired-world audio lost its copied prefix or remained active");
        (void)wire(
            "audio_capture",
            {{"operation", "end"}, {"generation", world}, {"capture_generation", second_capture}});
        while (response.at("audio_capture").at("queued_frames") != 0)
            response = read(world, second_capture);
        check(response.at("audio_capture").at("captured_frames") == cursor,
              "Retired-world PCM was lost or attributed to the new world");
    }
}

int run_live_audio_session_tests() {
    try {
        Fixture f;
        f.audio_enabled = true;
        f.audio_output = AudioOutput::NullDeviceTest;
        std::string session;
        auto host = build_activated(f, session);
        auto now = RuntimeClock::Clock::now();
        std::uint64_t id = 3, epoch = 0, cursor = 0;
        const auto wire = [&](const std::string& command, Json fields = Json::object()) {
            fields["session"] = session;
            return send(*host, ++id, command, fields, now);
        };
        auto state = wire("snapshot");
        const auto root = state.at("activation").at("generation");
        WorldContext first_world, second_world;
        Scene a(first_world), b(second_world);
        a.restore_snapshot(state.at("scene"));
        b.replace({{"version", 1}, {"entities", Json::array()}});
        std::filesystem::create_directories(f.project_root / "Scenes");
        a.save(f.project_root / "Scenes/live-a.scene.json");
        b.save(f.project_root / "Scenes/live-b.scene.json");
        AssetCatalog catalog(f.project_root);
        catalog.add_scene("Scenes/live-a.scene.json");
        catalog.add_scene("Scenes/live-b.scene.json");
        catalog.save(AssetCatalog::project_index(f.project_root));
        auto active_endpoint = f.capture.service;
        std::uint64_t rejected_ticket = 0;
        const auto drive = [&](bool accept_candidate = true) {
            now += std::chrono::milliseconds(10);
            host->pump(now);
            Json fields{
                {"editor_epoch",
                 {{"epoch", ++epoch}, {"captured", false}, {"input_device", "EditorHost"}}}};
            auto acknowledgements = Json::array();
            for (auto effect : host->live_effects()) {
                effect["session"] = session;
                effect["accepted"] = true;
                acknowledgements.push_back(std::move(effect));
            }
            if (!acknowledgements.empty()) {
                const auto accepted =
                    wire("snapshot", {{"platform_acks", std::move(acknowledgements)}});
                if (!accepted.value("ok", false))
                    throw std::runtime_error("Live session effect acknowledgement failed: " +
                                             accepted.value("error", std::string{}));
            }
            if (const auto candidate = host->pending_candidate()) {
                fields["candidate_ack"] = {{"session", session},
                                           {"ticket", candidate->at("ticket")},
                                           {"accepted", accept_candidate}};
                if (!accept_candidate)
                    rejected_ticket = candidate->at("ticket").get<std::uint64_t>();
            }
            auto response = wire("snapshot", fields);
            if (!response.value("ok", false))
                throw std::runtime_error("Live session host observation failed: " +
                                         response.value("error", std::string{}));
            return response;
        };
        check(
            !wire("audio_capture",
                  {{"operation", "begin"}, {"generation", root}, {"capture_scope", "unsupported"}})
                 .value("ok", true),
            "An unknown live scope was admitted");
        auto response =
            wire("audio_capture",
                 {{"operation", "begin"}, {"generation", root}, {"capture_scope", "session"}});
        check(response.value("ok", false), "Live session Begin was rejected");
        auto capture = response.at("audio_capture").at("generation");
        auto capture_world = root;
        const auto read = [&] {
            auto reply = wire("audio_capture", {{"operation", "read"},
                                                {"generation", capture_world},
                                                {"capture_generation", capture},
                                                {"audio_frame", cursor},
                                                {"maximum_frames", 4096u}});
            check(reply.value("ok", false), "Live session lost its original capture identity");
            const auto& chunk = reply.at("audio_capture_chunk");
            const auto frames = chunk.at("audio_frames").get<unsigned>();
            const auto bytes = decode_base64(chunk.at("data").get<std::string>(), 4096 * 4);
            check(chunk.at("start_audio_frame") == cursor && bytes.size() == frames * 4 &&
                      std::ranges::all_of(bytes,
                                          [](std::byte value) { return value == std::byte{}; }),
                  "Live session PCM was nonsequential or invented sound");
            cursor += frames;
            check(reply.at("audio_capture").at("read_frames") == cursor &&
                      reply.at("audio_capture").at("capture_scope") == "session" &&
                      reply.at("audio_capture").at("profile") == "live-session-game-v1",
                  "Live session counters/profile drifted");
            return reply;
        };
        const auto observe = [&](bool accept_candidate = true) {
            auto observed = drive(accept_candidate);
            // A recording client services its bounded PCM reads between World
            // observations, including background prepare/cancel/reject polling.
            do {
                response = read();
            } while (response.at("audio_capture").at("queued_frames") != 0);
            return observed;
        };
        const auto transition = [&](AssetId destination) {
            const auto previous = state.at("activation").at("generation");
            (void)active_endpoint->request("game.test", {{"operation", "prepare"},
                                                         {"asset", destination},
                                                         {"activate", true},
                                                         {"run", false}});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                state = observe();
                check(std::chrono::steady_clock::now() < deadline,
                      "Game-requested live recording Scene did not activate");
            } while (state.at("activation").at("generation") == previous);
            active_endpoint = f.capture.service;
            check(state.at("scene").at("asset_id").get<AssetId>() == destination &&
                      host->status().at("state") == "paused",
                  "Recording changed the requested Scene or its pause policy");
        };
        const auto wait_for_pcm = [&] {
            const auto before = cursor;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            do {
                response = read();
                check(response.at("audio_capture").at("state") == "recording",
                      "Live session failed before a Scene transition");
                check(std::chrono::steady_clock::now() < deadline,
                      "Live session did not observe its device mixer");
                if (cursor == before)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (cursor == before);
        };
        wait_for_pcm();
        for (const auto destination : {b.asset_id(), a.asset_id()}) {
            transition(destination);
            wait_for_pcm();
        }
        auto visits = response.at("audio_capture").at("visits");
        check(visits.size() == 3 && visits[0].at("scene").get<AssetId>() == a.asset_id() &&
                  visits[1].at("scene").get<AssetId>() == b.asset_id() &&
                  visits[2].at("scene").get<AssetId>() == a.asset_id() &&
                  visits[0].at("audio_frame") == 0u &&
                  visits[1].at("audio_frame") <= visits[2].at("audio_frame") &&
                  visits[2].at("audio_frame") <=
                      response.at("audio_capture").at("captured_frames") &&
                  response.at("audio_capture").at("generation") == capture &&
                  response.at("audio_capture").at("world_generation") == root,
              "Repeated Scene visits lost exact source/sample/capture identity");
        for (const auto& visit : visits)
            check(visit.at("source").get<std::string>().size() == 64,
                  "Live Scene visit has no bounded source witness");
        const auto unchanged_world = state.at("activation").at("generation");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        (void)active_endpoint->request("game.test", {{"operation", "prepare"},
                                                     {"asset", b.asset_id()},
                                                     {"activate", false},
                                                     {"run", false}});
        do {
            state = observe();
            check(std::chrono::steady_clock::now() < deadline,
                  "Background Scene preparation did not become pending");
        } while (!host->pending_candidate());
        const auto ticket = host->pending_candidate()->at("ticket");
        (void)active_endpoint->request("game.test", {{"operation", "cancel"}, {"ticket", ticket}});
        do {
            state = observe();
            check(std::chrono::steady_clock::now() < deadline,
                  "Background Scene preparation did not cancel");
        } while (host->pending_candidate());
        response = read();
        if (state.at("activation").at("generation") != unchanged_world ||
            response.at("audio_capture").at("state") != "recording" ||
            response.at("audio_capture").at("visits") != visits)
            throw std::runtime_error(
                "Cancelled preparation changed the live capture or its admitted visits: " +
                response.at("audio_capture").dump());
        (void)active_endpoint->request("game.test", {{"operation", "prepare"},
                                                     {"asset", b.asset_id()},
                                                     {"activate", true},
                                                     {"run", false}});
        do {
            state = observe(false);
            check(std::chrono::steady_clock::now() < deadline,
                  "Rejected Scene candidate was not retired");
        } while (!rejected_ticket || host->pending_candidate());
        response = read();
        if (state.at("activation").at("generation") != unchanged_world ||
            response.at("audio_capture").at("state") != "recording" ||
            response.at("audio_capture").at("visits") != visits)
            throw std::runtime_error(
                "Rejected preparation changed the live capture or its admitted visits: " +
                response.at("audio_capture").dump());
        for (const auto& fields : {Json{{"operation", "read"},
                                        {"generation", unchanged_world},
                                        {"capture_generation", capture},
                                        {"audio_frame", cursor},
                                        {"maximum_frames", 4096u}},
                                   Json{{"operation", "read"},
                                        {"generation", capture_world},
                                        {"capture_generation", capture},
                                        {"audio_frame", cursor + 1},
                                        {"maximum_frames", 4096u}},
                                   Json{{"operation", "end"},
                                        {"generation", capture_world},
                                        {"capture_generation", capture},
                                        {"capture_scope", "session"}}})
            check(!wire("audio_capture", fields).value("ok", true),
                  "Live session admitted stale identity, cursor or scope mutation");
        check(wire("audio_capture", {{"operation", "end"},
                                     {"generation", capture_world},
                                     {"capture_generation", capture}})
                  .value("ok", false),
              "Live session End failed");
        do {
            response = read();
        } while (response.at("audio_capture").at("queued_frames") != 0);
        check(response.at("audio_capture").at("captured_frames") == cursor,
              "Live session End lost its complete PCM prefix");

        capture_world = state.at("activation").at("generation");
        response = wire(
            "audio_capture",
            {{"operation", "begin"}, {"generation", capture_world}, {"capture_scope", "session"}});
        check(response.value("ok", false), "A drained live session did not restart");
        const auto previous_capture = capture;
        capture = response.at("audio_capture").at("generation");
        cursor = 0;
        check(capture != previous_capture, "A restarted session reused its capture token");
        for (unsigned visit = 1; visit < input_recording_scene_limit; ++visit) {
            transition(visit % 2 ? b.asset_id() : a.asset_id());
            do {
                response = read();
                const auto& progress = response.at("audio_capture");
                if (progress.at("state") != "recording" ||
                    progress.at("visits").size() != visit + 1)
                    throw std::runtime_error(
                        "Live visit " + std::to_string(visit) +
                        " failed: " + progress.at("error").get<std::string>() +
                        "; visits=" + std::to_string(progress.at("visits").size()) +
                        "; queued=" + std::to_string(progress.at("queued_frames").get<unsigned>()));
            } while (response.at("audio_capture").at("queued_frames") != 0);
        }
        transition(b.asset_id());
        response = read();
        check(response.at("audio_capture").at("state") == "failed" &&
                  response.at("audio_capture").at("visits").size() == input_recording_scene_limit &&
                  host->status().at("state") == "paused" &&
                  state.at("scene").at("asset_id").get<AssetId>() == b.asset_id(),
              "A recording visit limit blocked gameplay Scene adoption or exceeded its bound");
        (void)wire(
            "audio_capture",
            {{"operation", "end"}, {"generation", capture_world}, {"capture_generation", capture}});
        do {
            response = read();
        } while (response.at("audio_capture").at("queued_frames") != 0);
        {
            RuntimeWorld observed({}, {}, AudioConfig{f.project_root, AudioOutput::NullDeviceTest},
                                  f.project_root, false);
            RuntimeWorld no_audio({}, {}, std::nullopt, f.project_root, false);
            observed.scene.restore_snapshot(a.snapshot());
            no_audio.scene.restore_snapshot(b.snapshot());
            auto source_audio =
                std::static_pointer_cast<AudioRuntime>(observed.engine.services().audio());
            LiveAudioCapture observer(f.project_root);
            const auto begun = observer.command(
                {{"operation", "begin"}, {"generation", 1u}, {"capture_scope", "session"}}, 1,
                source_audio, &observed.scene, true);
            const auto observer_capture = begun.at("audio_capture").at("generation");
            const auto producer_deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (AudioCaptureAccess::status(*source_audio).at("captured_frames") == 0u) {
                check(std::chrono::steady_clock::now() < producer_deadline,
                      "Preparation failure fixture did not observe actual device PCM");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            bool delegate_activated = false;
            struct Ready final : GameScenePreparation {
                bool& activated;
                explicit Ready(bool& value) : activated(value) {}
                GamePreparationProgress poll(RuntimeWorld&) override {
                    return {true, "ready", 0, 0};
                }
                void activate() noexcept override { activated = true; }
            };
            auto preparation = observer.prepare(std::make_unique<Ready>(delegate_activated), 2);
            check(preparation->poll(no_audio).ready && !delegate_activated,
                  "Recording preparation failure rejected or activated its gameplay delegate");
            preparation->activate();
            check(delegate_activated, "Recording failure blocked gameplay delegate activation");
            const auto ended = observer.command({{"operation", "end"},
                                                 {"generation", 1u},
                                                 {"capture_generation", observer_capture}},
                                                2, {}, &no_audio.scene, true);
            check(
                ended.at("audio_capture").at("state") == "failed" &&
                    ended.at("audio_capture").at("visits").size() == 1 &&
                    ended.at("audio_capture").at("error").get<std::string>().find("preparation") !=
                        std::string::npos,
                "Recording preparation failure lost its prior World/source receipt");
            std::uint64_t retained = 0;
            auto progress = ended.at("audio_capture");
            while (progress.at("queued_frames") != 0) {
                const auto drained = observer.command({{"operation", "read"},
                                                       {"generation", 1u},
                                                       {"capture_generation", observer_capture},
                                                       {"audio_frame", retained},
                                                       {"maximum_frames", 4096u}},
                                                      2, {}, &no_audio.scene, true);
                retained += drained.at("audio_capture_chunk").at("audio_frames").get<unsigned>();
                progress = drained.at("audio_capture");
            }
            check(retained > 0 && progress.at("captured_frames") == retained,
                  "Recording preparation failure lost its retained PCM prefix");
        }
        std::cout << "Live session Scene/source/PCM identity, repeated visits, restart and "
                     "observer-only visit limit passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
