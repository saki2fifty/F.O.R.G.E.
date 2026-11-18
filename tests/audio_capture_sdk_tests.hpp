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
