// Included in the existing SDK Play fixture namespace.
void session_input_scene_transitions() {
    Fixture f;
    install_test_module(f);
    const auto action = ActionId::parse("23000000-0000-4230-8230-000000000001");
    std::vector<Json> consumed;
    EngineModule consumer;
    consumer.id = "test.session_input";
    consumer.runtime_roles = role_mask(WorldRole::Runtime);
    consumer.scene_ready = [&](ModuleContext& context) {
        auto* owner = &context;
        context.world.system()
            .kind(context.world.lookup("forge.runtime.Gameplay"))
            .run([&, owner](flecs::iter&) {
                const auto& value = owner->input->actions.at(action);
                consumed.push_back(
                    Json::array({owner->input->tick, value.held, value.pressed, value.released}));
            })
            .add<FixedSimulation>();
    };
    f.modules.push_back(std::move(consumer));
    auto config = make_config(f);
    config.input = InputMap(
        {{"version", 1},
         {"actions", Json::array({{{"id", action},
                                   {"name", "Session action"},
                                   {"kind", "digital"},
                                   {"bindings", Json::array({{{"control", "key.space"}}})}}})}});
    WorldContext world_a, world_b;
    Scene scene_a(world_a), scene_b(world_b);
    scene_a.replace({{"version", 1}, {"entities", Json::array()}});
    scene_b.replace({{"version", 1}, {"entities", Json::array()}});
    std::filesystem::create_directories(f.project_root / "Scenes");
    scene_a.save(f.project_root / "Scenes/a.scene.json");
    scene_b.save(f.project_root / "Scenes/b.scene.json");
    AssetCatalog catalog(f.project_root);
    catalog.add_scene("Scenes/a.scene.json");
    catalog.add_scene("Scenes/b.scene.json");
    catalog.save(AssetCatalog::project_index(f.project_root));

    auto now = RuntimeClock::Clock::now();
    std::unique_ptr<SdkPlayRuntime> host;
    Json session;
    std::uint64_t sequence = 1, epoch = 0;
    unsigned admissions = 0;
    const auto wire = [&](const std::string& command, Json fields = Json::object()) {
        fields["session"] = session;
        return send(*host, ++sequence, command, fields, now);
    };
    const auto snapshot = [&] { return wire("snapshot"); };
    const auto pump = [&] {
        now += std::chrono::milliseconds(1);
        host->pump(now);
        auto acknowledgements = Json::array();
        for (const auto& effect : host->live_effects()) {
            check(effect.at("kind").get<std::string>() == "cursor" &&
                      effect.at("value") == Json(false),
                  "Session fixture unexpectedly requested a physical platform change");
            auto ack = effect;
            ack["session"] = session;
            ack["accepted"] = true;
            acknowledgements.push_back(std::move(ack));
        }
        if (!acknowledgements.empty())
            check(wire("snapshot", {{"platform_acks", acknowledgements}}).value("ok", false),
                  "Session fixture could not acknowledge its already-released cursor");
    };
    const auto admit = [&] {
        ++admissions;
        for (unsigned i = 0; i < 100 && !host->pending_candidate(); ++i)
            pump();
        if (!host->pending_candidate())
            throw std::runtime_error("Session Scene candidate never published at admission " +
                                     std::to_string(admissions) + ": " + host->loading().dump() +
                                     " | " + host->status().dump());
        const auto ticket = host->pending_candidate()->at("ticket");
        check(wire("candidate", {{"ticket", ticket}}).value("ok", false),
              "Session Scene candidate could not be fetched");
        check(
            wire("snapshot",
                 {{"candidate_ack", {{"session", session}, {"ticket", ticket}, {"accepted", true}}},
                  {"editor_epoch",
                   {{"epoch", ++epoch}, {"captured", false}, {"input_device", "EditorHost"}}}})
                .value("ok", false),
            "Session Scene candidate acknowledgement failed");
        for (unsigned i = 0; i < 100 && snapshot().at("activation").at("generation") != ticket; ++i)
            pump();
        check(snapshot().at("activation").at("generation") == ticket,
              "Session Scene did not activate after acknowledgement");
        check(wire("snapshot",
                   {{"editor_epoch",
                     {{"epoch", ++epoch}, {"captured", false}, {"input_device", "EditorHost"}}}})
                  .value("ok", false),
              "Session Scene did not accept released editor observation");
    };
    const auto launch = [&] {
        host = std::make_unique<SdkPlayRuntime>(config);
        session = hello(*host).at("session");
        sequence = 1;
        epoch = 0;
        check(wire("replace", {{"scene", scene_a.snapshot()}}).value("ok", false),
              "Session fixture could not prepare its initial Scene");
        admit();
    };
    const auto input_command = [&](const std::string& operation, Json extra = Json::object()) {
        extra["operation"] = operation;
        extra["generation"] = snapshot().at("activation").at("generation");
        return wire("input_recording", extra);
    };
    const auto step = [&](Json events = Json::array()) {
        Json fields = Json::object();
        if (!events.empty())
            fields["input_events"] = std::move(events);
        check(wire("step", fields).value("ok", false), "Session input Step failed");
    };
    const auto prepare = [&](AssetId asset) {
        // Request through the real active-world mailbox and content catalog.
        // Its endpoint retires during activation; never inspect it afterwards.
        const auto service = f.capture.service;
        service->request(
            "game.test",
            {{"operation", "prepare"}, {"asset", asset}, {"activate", true}, {"run", false}});
        pump();
    };
    launch();
    check(input_command("begin", {{"capture_scope", "session"}}).value("ok", false),
          "Across-Scene capture did not begin");
    step(Json::array({{{"control", "key.space"}, {"value", 1}}}));
    step(Json::array({{{"reset", true}}}));
    prepare(scene_b.asset_id());
    admit();
    step(Json::array({{{"control", "key.space"}, {"value", 1}}}));
    prepare(scene_a.asset_id());
    admit(); // A repeated visit with zero fixed ticks must remain in the take.
    prepare(scene_b.asset_id());
    admit();
    step();
    check(input_command("finish").value("ok", false), "Session capture did not finish");
    const auto document = input_command("read").at("input_recording_document");
    const auto expected = consumed;
    check(document.at("segments").size() == 4 &&
              document.at("segments").at(2).at("frames").empty() && expected.size() == 4,
          "Session capture lost repeated or tick-zero Scene segments");
    host.reset();
    consumed.clear();

    launch();
    check(input_command("replay", {{"document", document}}).value("ok", false),
          "Fresh multi-Scene SDK replay was rejected");
    prepare(scene_b.asset_id());
    check(!host->pending_candidate(), "Replay published the next Scene before consuming input");
    step();
    pump();
    check(!host->pending_candidate(), "Replay skipped its current Scene's last input tick");
    step();
    check(snapshot().at("input").at("recording").at("waiting_for_scene").get<bool>(),
          "Replay did not wait at its Scene boundary");
    check(!wire("step").value("ok", true), "Step advanced a waiting replay boundary");
    admit();
    step();
    prepare(scene_a.asset_id());
    admit();
    check(snapshot().at("input").at("recording").at("waiting_for_scene").get<bool>(),
          "Tick-zero repeated Scene replay did not wait for its successor");
    prepare(scene_b.asset_id());
    admit();
    step();
    check(consumed == expected &&
              snapshot().at("input").at("recording").at("mode").get<std::string>() == "finished",
          "Real SDK Scene transitions did not replay the original native input sequence");
    step(Json::array({{{"control", "key.space"}, {"value", 1}}}));
    check(!consumed.back().at(1).get<bool>(), "Finished session replay admitted live input");
    host.reset();

    launch();
    auto changed = document;
    changed["segments"][1]["source"] = std::string(64, '0');
    check(input_command("replay", {{"document", changed}}).value("ok", false),
          "Valid replay prefix was rejected before its later witness could be checked");
    step();
    step();
    const auto previous = snapshot().at("activation").at("generation");
    prepare(scene_b.asset_id());
    for (unsigned i = 0; i < 5; ++i)
        pump();
    const auto rejected = snapshot();
    check(rejected.at("activation").at("generation") == previous &&
              rejected.at("input").at("recording").at("waiting_for_scene").get<bool>(),
          "A mismatching next-Scene source replaced the active replay owner");
    check(input_command("finish").value("ok", false), "Waiting session replay could not cancel");
    step(Json::array({{{"control", "key.space"}, {"value", 1}}}));
    check(!consumed.back().at(1).get<bool>(), "Canceled session replay admitted live input");
}

void input_recording_sdk_tests(bool session_capture = false) {
    Fixture f;
    f.audio_enabled = true;
    const auto id = ActionId::parse("23000000-0000-4230-8230-000000000001");
    const InputMap input(
        {{"version", 1},
         {"actions", Json::array({{{"id", id},
                                   {"name", "Move"},
                                   {"kind", "digital"},
                                   {"bindings", Json::array({{{"control", "key.space"}}})}}})}});
    std::vector<Json> consumed;
    unsigned position = 0;
    EngineModule module;
    module.id = "test.input_recording";
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.scene_ready = [&](ModuleContext& context) {
        auto* owner = &context;
        auto system =
            context.world.system()
                .kind(context.world.lookup("forge.runtime.Gameplay"))
                .run([&, owner](flecs::iter&) {
                    const auto& action = owner->input->actions.at(id);
                    if (action.held)
                        ++position;
                    consumed.push_back(Json::array({owner->input->tick, action.x, action.held,
                                                    action.pressed, action.released, position}));
                });
        system.add<FixedSimulation>();
    };
    f.modules.push_back(std::move(module));
    auto config = make_config(f);
    config.input = input;
    WorldContext author;
    Scene authored(author);
    authored.replace({{"version", 1}, {"entities", Json::array()}});
    const auto scene = authored.snapshot();
    const auto activate = [&](bool offline = false) {
        auto launch = config;
        launch.recording = offline;
        auto host = std::make_unique<SdkPlayRuntime>(std::move(launch));
        const auto session = hello(*host).at("session");
        check(
            send(*host, 2, "replace", {{"session", session}, {"scene", scene}}).value("ok", false),
            "Input recording fixture could not prepare");
        host->pump(RuntimeClock::Clock::now());
        const auto candidate = host->pending_candidate().value();
        check(
            send(*host, 3, "snapshot",
                 {{"session", session},
                  {"candidate_ack",
                   {{"session", session}, {"ticket", candidate.at("ticket")}, {"accepted", true}}}})
                .value("ok", false),
            "Input recording fixture could not acknowledge");
        host->pump(RuntimeClock::Clock::now());
        return std::pair{std::move(host), session};
    };
    auto [record, session] = activate();
    std::uint64_t sequence = 3;
    auto now = RuntimeClock::Clock::now();
    const auto wire = [&](SdkPlayRuntime& host, const Json& token, std::string command,
                          Json fields = Json::object()) {
        fields["session"] = token;
        return send(host, ++sequence, command, fields, now);
    };
    const auto generation = wire(*record, session, "snapshot").at("activation").at("generation");
    for (const auto& invalid : {Json("invalid"), Json(), Json(1)})
        check(
            !wire(*record, session, "input_recording",
                  {{"generation", generation}, {"operation", "begin"}, {"capture_scope", invalid}})
                 .value("ok", true),
            "Malformed recording scope was admitted");
    check(wire(*record, session, "input_recording",
               {{"generation", generation},
                {"operation", "begin"},
                {"capture_scope", session_capture ? "session" : "scene"}})
              .value("ok", false),
          "SDK input recording did not begin");
    check(wire(*record, session, "resume").value("ok", false), "SDK recording could not resume");
    for (const auto& events : {Json::array({{{"control", "key.space"}, {"value", 1}}}),
                               Json::array(), Json::array({{{"reset", true}}}),
                               Json::array({{{"control", "key.space"}, {"value", 1}},
                                            {{"control", "key.space"}, {"value", 0}}})}) {
        auto fields = Json::object();
        if (!events.empty())
            fields["input_events"] = events;
        check(wire(*record, session, "snapshot", fields).value("ok", false),
              "SDK record input failed");
        now += std::chrono::nanoseconds(16666667);
        record->pump(now);
    }
    check(!wire(*record, session, "input_recording",
                {{"generation", generation}, {"operation", "finish"}, {"capture_scope", "scene"}})
               .value("ok", true),
          "Finish accepted a capture-only scope argument");
    check(wire(*record, session, "input_recording",
               {{"generation", generation}, {"operation", "finish"}})
              .value("ok", false),
          "SDK input recording did not finish");
    const auto response = wire(*record, session, "input_recording",
                               {{"generation", generation}, {"operation", "read"}});
    check(response.value("ok", false), "SDK input recording read failed");
    const auto document = response.at("input_recording_document");
    check(document.at("version").get<unsigned>() == (session_capture ? 2u : 1u),
          "SDK capture returned the wrong recording profile");
    const auto expected = consumed;
    check(position == 2 && expected.size() == 4,
          "Native consumer did not observe recorded actions");
    record.reset();
    position = 0;
    consumed.clear();
    auto [replay, replay_session] = activate();
    sequence = 3;
    now = RuntimeClock::Clock::now();
    const auto replay_generation =
        wire(*replay, replay_session, "snapshot").at("activation").at("generation");
    check(wire(*replay, replay_session, "input_recording",
               {{"generation", replay_generation}, {"operation", "replay"}, {"document", document}})
              .value("ok", false),
          "Fresh SDK replay was not admitted");
    check(wire(*replay, replay_session, "resume").value("ok", false),
          "SDK replay could not resume");
    for (unsigned i = 0; i < expected.size(); ++i) {
        check(wire(*replay, replay_session, "snapshot",
                   {{"input_events", Json::array({{{"control", "key.space"}, {"value", 1}}})}})
                  .value("ok", false),
              "SDK replay step failed");
        now += std::chrono::nanoseconds(16666667);
        replay->pump(now);
    }
    check(consumed == expected && position == 2,
          "Fresh SDK replay did not drive the same native gameplay consumer");
    replay.reset();
    position = 0;
    consumed.clear();
    auto [movie, movie_session] = activate(true);
    sequence = 3;
    now = RuntimeClock::Clock::now();
    const auto movie_generation =
        wire(*movie, movie_session, "snapshot").at("activation").at("generation");
    check(!wire(*movie, movie_session, "input_recording",
                {{"generation", movie_generation}, {"operation", "begin"}})
               .value("ok", true),
          "Offline movie admitted interactive input capture");
    if (session_capture) {
        check(!wire(*movie, movie_session, "input_recording",
                    {{"generation", movie_generation},
                     {"operation", "replay"},
                     {"document", document}})
                   .value("ok", true),
              "Single-Scene movie admitted an across-Scenes take");
        const auto status = wire(*movie, movie_session, "snapshot");
        check(status.at("input").at("recording").at("mode").get<std::string>() == "idle",
              "Rejected movie replay changed its input owner");
        return;
    }
    check(wire(*movie, movie_session, "input_recording",
               {{"generation", movie_generation}, {"operation", "replay"}, {"document", document}})
              .value("ok", false),
          "Offline movie rejected the saved input replay");
    check(wire(*movie, movie_session, "record_begin",
               {{"generation", movie_generation}, {"fps", 30}, {"frames", 3u}})
              .value("ok", false),
          "Input-driven offline movie did not begin");
    for (unsigned frame = 0; frame < 3; ++frame)
        check(wire(*movie, movie_session, "record_frame",
                   {{"generation", movie_generation}, {"frame", frame}})
                  .value("ok", false),
              "Input-driven movie frame failed");
    check(consumed.size() == 6 && position == 2 &&
              std::equal(expected.begin(), expected.end(), consumed.begin()),
          "Offline media clock changed the saved gameplay input sequence");

    GameSessionConfig session_config;
    session_config.input = input;
    GameSession guarded(session_config);
    auto ticket = guarded.prepare(scene);
    guarded.activate(ticket, now, false);
    const auto pending = guarded.prepare(scene);
    guarded.active().simulation.input().begin_input_recording("scene-source", 60);
    bool rejected_prepare = false, rejected_activate = false;
    try {
        (void)guarded.prepare(scene);
    } catch (const std::exception&) {
        rejected_prepare = true;
    }
    try {
        guarded.activate(pending, now, false);
    } catch (const std::exception&) {
        rejected_activate = true;
    }
    check(rejected_prepare && rejected_activate &&
              guarded.status().at("prepared_ticket") == pending &&
              guarded.active().simulation.input().input_recording_status().at("mode") ==
                  "recording",
          "Scene replacement bypassed input-recording ownership or changed the active run");
}
