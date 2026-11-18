// Included in the native recording editor fixture.
void pump_live_host(PlaySession& play, std::uint64_t& acknowledged) {
    play.pump();
    if (play.sdk_candidate_ticket() && play.sdk_candidate_ticket() != acknowledged &&
        !play.sdk_candidate_envelope().is_null()) {
        check(play.submit_editor_observation(false, "KeyboardMouse") &&
                  play.submit_sdk_candidate_ack(true, play.session(), play.sdk_candidate_ticket()),
              "Live audio fixture could not admit its existing Play world");
        acknowledged = play.sdk_candidate_ticket();
    }
    auto acks = Json::array();
    for (auto effect : play.sdk_platform_effects()) {
        effect["session"] = play.session();
        effect["accepted"] = true;
        acks.push_back(std::move(effect));
    }
    if (!acks.empty())
        check(play.submit_sdk_platform_acks(std::move(acks)),
              "Live audio fixture could not acknowledge its uncaptured input state");
}
void live_audio_transport(const std::filesystem::path& runtime, const std::filesystem::path& root) {
    const auto project = root / "LiveGame";
    SceneDocument::create_project(project, "Live audio transport");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(project);
    auto settings = document.settings().document();
    settings["game"] = default_game_settings("org.forge.live-audio-" + AssetId::generate().str(),
                                             "Live audio transport");
    settings["input"] = {
        {"version", 1},
        {"actions", Json::array({{{"id", AssetId::generate().str()},
                                  {"name", "Live action"},
                                  {"kind", "digital"},
                                  {"bindings", Json::array({{{"control", "key.space"}}})}}})}};
    document.save_settings(settings);
    document.save();
    const auto original = scene.snapshot();
    const auto revision = scene.revision();
    PlaySession play;
    play.configure(60, document.settings().input(), {0, -9.81, 0}, project, true, true);
    play.set_headless_audio(true, true);
    play.set_user_data_override(root / "LiveUserData");
    play.start(path_utf8(runtime), scene.snapshot());
    bool drain = false;
    std::uint64_t cursor = 0, acknowledged = 0;
    const auto frame = [&] {
        pump_live_host(play, acknowledged);
        if (auto pcm = play.take_live_audio()) {
            check(pcm->start_audio_frame == cursor && pcm->pcm.size() % 4 == 0,
                  "Editor live PCM was repeated or skipped");
            cursor += pcm->pcm.size() / 4;
        }
        check(play.live_audio_error().empty(),
              "Live audio transport rejected a request: " + play.live_audio_error());
        if (drain && play.control_ready() && !play.live_audio_pending())
            check(play.submit_live_audio("read"), "Editor could not queue its next live PCM read");
        SDL_Delay(1);
    };
    const auto wait = [&](const auto& predicate) {
        const auto deadline = SDL_GetTicks() + 15000;
        do {
            frame();
            check(play.active(), "Ordinary Play stopped during audio capture: " + play.status());
            if (predicate())
                return;
        } while (SDL_GetTicks() < deadline);
        throw std::runtime_error("Live audio editor wait timed out: " + play.status() + " | " +
                                 play.live_audio_status().dump());
    };
    wait([&] { return play.control_ready() && !play.paused() && play.timing().at("tick") > 0; });
    check(play.live_audio_supported() && play.submit_live_audio("begin"),
          "Matching SDK Play did not support live mixer capture");
    wait([&] { return !play.live_audio_pending() && play.live_audio_status().is_object(); });
    const auto capture = play.live_audio_status().at("generation");
    drain = true;
    play.input_event({"key.space", 1});
    wait([&] {
        return cursor > 0 && play.input_status().at("actions").at(0).value("presses", 0u) > 0;
    });
    play.input_event({"key.space", 0});
    wait([&] { return play.input_status().at("actions").at(0).value("releases", 0u) > 0; });
    wait([&] { return play.control_ready(); });
    play.pause();
    wait([&] { return play.paused(); });
    const auto paused_tick = play.timing().at("tick");
    const auto before_pause_audio = cursor;
    wait([&] { return cursor > before_pause_audio; });
    check(play.timing().at("tick") == paused_tick,
          "Live audio changed the paused simulation clock");
    wait([&] { return play.control_ready(); });
    play.resume();
    wait([&] { return !play.paused() && play.timing().at("tick") > paused_tick; });
    drain = false;
    wait([&] { return !play.live_audio_pending() && play.control_ready(); });
    check(play.submit_live_audio("end"), "Editor could not end live audio capture");
    wait([&] {
        return !play.live_audio_pending() && play.live_audio_status().at("state") == "stopped";
    });
    while (play.live_audio_status().at("queued_frames") != 0) {
        wait([&] { return play.control_ready(); });
        check(play.submit_live_audio("read"), "Editor could not drain its final live PCM");
        wait([&] { return !play.live_audio_pending(); });
    }
    check(play.live_audio_status().at("captured_frames") == cursor && play.active() &&
              !play.paused() && scene.snapshot() == original && scene.revision() == revision,
          "Live capture lost audio, stopped Play or changed authored data");
    wait([&] { return play.control_ready(); });
    check(play.submit_live_audio("begin"), "Editor could not restart a drained live capture");
    wait([&] {
        return !play.live_audio_pending() && play.live_audio_status().at("generation") != capture;
    });
    wait([&] { return play.control_ready(); });
    check(play.submit_live_audio("end"), "Editor could not stop its second live capture");
    wait([&] { return !play.live_audio_pending(); });
    play.stop();
    check(!play.live_audio_supported() && play.live_audio_status().is_null() &&
              !play.take_live_audio(),
          "Stopping Play retained capture capability, data or identity");
    std::cout
        << "Actual SDK Play live mixer reads preserve input, Pause/Resume and authored state\n";
}
