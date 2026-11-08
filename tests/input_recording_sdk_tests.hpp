// Included in the existing SDK Play fixture namespace.
void input_recording_sdk_tests() {
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
    check(wire(*record, session, "input_recording",
               {{"generation", generation}, {"operation", "begin"}})
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
    check(wire(*record, session, "input_recording",
               {{"generation", generation}, {"operation", "finish"}})
              .value("ok", false),
          "SDK input recording did not finish");
    const auto response = wire(*record, session, "input_recording",
                               {{"generation", generation}, {"operation", "read"}});
    check(response.value("ok", false), "SDK input recording read failed");
    const auto document = response.at("input_recording_document");
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
