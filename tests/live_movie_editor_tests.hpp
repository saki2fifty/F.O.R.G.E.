// Included in the native recording editor fixture after the live transport helpers.
void live_movie_workflow(const std::filesystem::path& runtime, const std::filesystem::path& worker,
                         const std::filesystem::path& root) {
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument document(scene);
    document.open_project(root / "LiveGame");
    const auto original = scene.snapshot();
    const auto revision = scene.revision();
    PlaySession play;
    play.configure(60, document.settings().input(), {0, -9.81, 0}, document.project(), true, true);
    play.set_headless_audio(true, true);
    play.set_user_data_override(root / "LiveMovieUserData");
    play.start(path_utf8(runtime), scene.snapshot());
    std::uint64_t acknowledged = 0;
    const auto launch = play.launch_id(), user_data = play.user_data_override();
    const auto ready = SDL_GetTicks() + 15000;
    while ((!play.control_ready() || play.paused()) && SDL_GetTicks() < ready) {
        pump_live_host(play, acknowledged);
        SDL_Delay(1);
    }
    check(play.control_ready() && !play.paused(), "Live movie Play did not become ready");
    bool restarted = false, stopped = false;
    std::string source_unavailable;
    ui::GameRecordingHost host;
    host.unavailable = [&] { return source_unavailable; };
    host.start = [&](const std::filesystem::path&) { restarted = true; };
    host.stop = [&] { stopped = true; };
    host.pixels = [&](unsigned) {
        const auto tick = play.timing().at("tick").get<unsigned>();
        Rgba8Pixels pixels{7, 5, std::vector<std::uint8_t>(140), true};
        for (unsigned i = 0; i < 35; ++i) {
            pixels.rgba[i * 4] = std::uint8_t(tick % 256);
            pixels.rgba[i * 4 + 3] = 255;
        }
        ui::ViewImageTask::PixelInput source;
        source.source = {{"fixture", "detached current Game snapshot"}};
        source.load = [pixels = std::move(pixels)](ViewImageFormat) {
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            return pixels;
        };
        return source;
    };
    ui::GameRecordingRequest request;
    request.recording_mode = "live";
    request.fps = 30;
    request.output.width = 7;
    request.output.height = 5;
    std::filesystem::create_directory(root / "LiveMovies");
    for (const bool cancel : {false, true}) {
        request.destination = root / "LiveMovies" / (cancel ? "Cancelled" : "Complete");
        request.frames = cancel ? 600 : 9;
        ui::ViewImageTask images(worker);
        std::string message;
        auto task =
            std::make_unique<ui::GameRecordingTask>(scene, document, play, worker, request, host);
        auto* recording = task.get();
        check(images.begin_work(std::move(task), document.project(), scene.asset_id(), message),
              "Live movie did not enter the existing capture owner");
        bool cancelled = false;
        const auto deadline = SDL_GetTicks() + 20000;
        while (images.busy() && SDL_GetTicks() < deadline) {
            pump_live_host(play, acknowledged);
            images.poll_work(nullptr, message);
            if (cancel && !cancelled && recording->state().at("captured_png_frames") > 0) {
                check(images.cancel(message), "Live movie Cancel was refused");
                cancelled = true;
            }
            SDL_Delay(1);
        }
        check(!images.busy(),
              "Live movie did not settle: " + recording->state().dump() + " | " + play.status());
        const auto receipt =
            Json::parse(asset_storage::read(request.destination / "recording.json").value());
        check(receipt.at("version") == 3 && receipt.at("mode") == "live_game" &&
                  receipt.at("state") == (cancel ? "cancelled" : "complete") && play.active() &&
                  play.launch_id() == launch && !play.paused() &&
                  play.user_data_override() == user_data && !restarted && !stopped &&
                  !std::filesystem::exists(request.destination / "UserData") &&
                  scene.snapshot() == original && scene.revision() == revision &&
                  play.live_audio_status().at("state") == "stopped" &&
                  play.live_audio_status().at("queued_frames") == 0,
              "Live recording changed Play/source/user data or leaked its tap: " + receipt.dump());
        check(receipt.at("images").size() == receipt.at("captured_png_frames").get<unsigned>(),
              "Live image provenance differs from its saved PNG count");
        check(receipt.at("frames").size() == receipt.at("completed_frames").get<unsigned>(),
              "Live receipt counted an image reference without its retained audio interval");
        if (cancel)
            check(!std::filesystem::exists(request.destination / "game.avi"),
                  "Cancelled live movie was published");
        else {
            check(
                receipt.at("completed_frames") == 9 && receipt.at("frames").size() == 9 &&
                    receipt.at("audio_frames") == 14400 && receipt.at("audio_normalized") == true &&
                    std::filesystem::file_size(request.destination / "audio.s16le") == 14400 * 4 &&
                    std::filesystem::exists(request.destination / "game.avi"),
                "Live AVI/PCM do not match their exact container duration");
            bool repeated = false;
            for (unsigned i = 0; i < 9; ++i) {
                const auto& row = receipt.at("frames").at(i);
                const auto& image = receipt.at("images").at(row.at("image").get<unsigned>());
                check(row.at("file") == image.at("file") &&
                          row.at("simulation_tick") == image.at("simulation_tick") &&
                          row.at("audio_start_frame") == i * 1600u &&
                          row.at("source_audio_frame").get<std::uint64_t>() <= i * 1600u &&
                          !image.at("captured_at").get<std::string>().empty(),
                      "Live frame invented a capture, source tick or future sample");
                repeated = repeated || row.at("repeated").get<bool>();
            }
            check(repeated, "Delayed image IO did not exercise repeated video references");
        }
    }
    for (const std::string failure : {"SourceChanged", "OwnerClosed", "PlayStopped"}) {
        request.destination = root / "LiveMovies" / failure;
        request.frames = 600;
        auto recording =
            std::make_unique<ui::GameRecordingTask>(scene, document, play, worker, request, host);
        bool triggered = false;
        const auto deadline = SDL_GetTicks() + 15000;
        while (SDL_GetTicks() < deadline) {
            pump_live_host(play, acknowledged);
            if (recording)
                recording->poll(nullptr);
            if (!triggered && recording->state().at("captured_png_frames") > 0) {
                triggered = true;
                if (failure == "SourceChanged")
                    source_unavailable = "Project source changed during capture";
                else if (failure == "OwnerClosed")
                    recording.reset();
                else
                    play.stop();
            }
            if (triggered && (!recording || !recording->busy()) &&
                (!play.active() || (!play.live_audio_pending() && !play.live_audio_retiring() &&
                                    play.live_audio_status().at("state") != "recording" &&
                                    play.live_audio_status().at("queued_frames") == 0)))
                break;
            SDL_Delay(1);
        }
        check(triggered && (!recording || !recording->busy()) &&
                  (!play.active() || (!play.live_audio_retiring() &&
                                      play.live_audio_status().at("state") != "recording" &&
                                      play.live_audio_status().at("queued_frames") == 0)),
              "Live failure left the mixer observer active: " + failure);
        const auto receipt =
            Json::parse(asset_storage::read(request.destination / "recording.json").value());
        check(receipt.at("state") == (failure == "OwnerClosed" ? "cancelled" : "failed") &&
                  receipt.at("captured_png_frames") > 0 &&
                  receipt.at("frames").size() == receipt.at("completed_frames").get<unsigned>() &&
                  std::filesystem::file_size(request.destination / "audio.s16le") ==
                      receipt.at("audio_frames").get<std::uint64_t>() * 4 &&
                  !std::filesystem::exists(request.destination / "game.avi") &&
                  scene.snapshot() == original && scene.revision() == revision && !stopped &&
                  !restarted &&
                  (failure == "PlayStopped" ||
                   (play.active() && play.launch_id() == launch &&
                    play.user_data_override() == user_data && !play.paused())),
              "Live failure lost partial output or changed the original Play/source: " + failure);
        source_unavailable.clear();
    }
    play.stop();
    std::cout
        << "Live movie uses the existing launch, actual PCM and repeated saved image references\n";
}
