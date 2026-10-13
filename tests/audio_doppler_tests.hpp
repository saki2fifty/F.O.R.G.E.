#pragma once
#include <limits>
// Independent frequency measurement from actual offline PCM, not backend status.
double audio_frequency(Fixture& fixture) {
    (void)fixture.energy(4096);
    std::vector<float> pcm(48000 * 2);
    fixture.audio->read_offline(pcm);
    unsigned crossings = 0, first = 0, last = 0;
    double previous = pcm[0] + pcm[1];
    for (unsigned frame = 1; frame < 48000; ++frame) {
        const double sample = double(pcm[frame * 2]) + pcm[frame * 2 + 1];
        check(std::isfinite(sample), "Motion produced nonfinite PCM");
        if (previous <= 0 && sample > 0) {
            if (crossings++ == 0)
                first = frame;
            last = frame;
        }
        previous = sample;
    }
    check(crossings > 100 && last > first, "Motion frequency reference is silent");
    return double(crossings - 1) * 48000 / (last - first);
}
void doppler_tests(const std::filesystem::path& root, AssetId clip) {
    auto document = source(clip);
    auto& fields = document["entities"][0]["components"]["forge.audio_source"];
    fields["spatialized"] = true;
    fields["rolloff"] = 0;
    fields["doppler"] = 1;
    document["entities"][0]["components"]["forge.position"]["z"] = 10;
    document["entities"].push_back({{"id", "listener"},
                                    {"name", "Listener"},
                                    {"components",
                                     {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                      {"forge.audio_listener", {{"enabled", true}}}}}});
    constexpr double sound_speed = 343.3;
    constexpr float dt = 1.f / 60;
    for (const auto velocities :
         {std::array{0., 0.}, std::array{-33., 0.}, std::array{33., 0.}, std::array{0., 33.},
          std::array{0., -33.}, std::array{33., 33.}}) {
        Fixture f(root);
        f.load(document);
        f.simulation.audio_paused(false);
        auto actor = f.scene.entity("speaker");
        auto listener = f.scene.entity("listener");
        actor.set<LocalTranslation>({0, 0, 10 + velocities[0] * dt});
        listener.set<LocalTranslation>({0, 0, velocities[1] * dt});
        f.tick();
        const auto expected = 440 * (sound_speed + velocities[1]) / (sound_speed + velocities[0]);
        check(std::abs(audio_frequency(f) - expected) < .5,
              "Motion PCM differs from independent source/listener Doppler equation");
        check(f.audio->status().at("voices") == 1 && f.audio->status().at("clips") == 1,
              "Motion replaced voice or decoded clip");
    }
    Fixture f(root);
    // Servicing a different clip between fixed ticks must retain the moving
    // voice's last fixed velocity, rather than introduce nominal-pitch gaps.
    {
        wav(root / "Assets/loading-doppler.wav");
        const auto other = AssetCatalog::register_audio_clip(root, "Assets/loading-doppler.wav");
        Fixture loading(root);
        loading.load(document);
        loading.simulation.audio_paused(false);
        AudioSource silent;
        silent.clip.id = other.id;
        silent.gain = 0;
        silent.spatialized = false;
        const auto id = authoring_command(loading.scene, "entity.create", {{"name", "Loading"}})
                            .at("selected")
                            .get<std::string>();
        loading.scene.entity(id).set(silent);
        loading.scene.entity("speaker").set<LocalTranslation>({0, 0, 10 - 33 * dt});
        loading.simulation.tick(dt);
        check(loading.audio->status().at("pending_sources") == 1,
              "Doppler loading fixture did not create an independent request");
        const auto expected = 440 * sound_speed / (sound_speed - 33);
        check(std::abs(audio_frequency(loading) - expected) < .5,
              "Pending load changed the initial fixed velocity");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (loading.audio->status().at("pending_sources") != 0) {
            loading.audio->poll_loading();
            check(std::chrono::steady_clock::now() < deadline, "Doppler clip admission stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(loading.audio->status().at("tick") == 1 &&
                  std::abs(audio_frequency(loading) - expected) < .5,
              "Between-tick admission changed motion pitch or advanced time");
        loading.simulation.sync_audio();
        check(std::abs(audio_frequency(loading) - 440) < .5,
              "Explicit repeated pose boundary did not clear motion");
    }
    for (float delta : {1.f / 30, 1.f / 120}) {
        auto scaled = document;
        scaled["entities"][0]["components"]["forge.audio_source"]["doppler"] = 2;
        scaled["entities"][0]["components"]["forge.audio_source"]["pitch"] = 2;
        Fixture different_rate(root);
        different_rate.load(scaled);
        different_rate.simulation.audio_paused(false);
        different_rate.scene.entity("speaker").set<LocalTranslation>({0, 0, 10 - 33 * delta});
        different_rate.simulation.tick(delta);
        check(std::abs(audio_frequency(different_rate) - 880 * sound_speed / (sound_speed - 66)) <
                  .5,
              "Fixed tick rate, authored pitch or Doppler strength changed relative velocity "
              "semantics");
    }
    f.load(document);
    f.simulation.audio_paused(false);
    auto actor = f.scene.entity("speaker");
    actor.set<LocalTranslation>({0, 0, 10 - 33 * dt});
    f.tick();
    check(audio_frequency(f) > 480, "Approaching emitter did not raise pitch");
    // Repeated/admission boundary establishes a new pose without inventing motion.
    actor.set<LocalTranslation>({0, 0, 300});
    f.simulation.sync_audio();
    check(std::abs(audio_frequency(f) - 440) < .5, "Repeated boundary retained stale velocity");
    actor.set<LocalTranslation>({0, 0, 400});
    const std::array discontinuities{actor.id()};
    f.audio->synchronize(f.audio->status().at("tick").get<std::uint64_t>() + 1, dt,
                         discontinuities);
    check(std::abs(audio_frequency(f) - 440) < .5, "Teleport boundary produced motion pitch");
    reject([&] { f.audio->synchronize(50, std::numeric_limits<float>::quiet_NaN()); });
    reject([&] { f.audio->synchronize(50, -1); });

    // Default/disabled and nonspatial sources keep their nominal pitch.
    for (bool spatial : {true, false}) {
        auto unchanged = document;
        unchanged["entities"][0]["components"]["forge.audio_source"]["spatialized"] = spatial;
        if (spatial)
            unchanged["entities"][0]["components"]["forge.audio_source"].erase("doppler");
        Fixture disabled(root);
        disabled.load(unchanged);
        disabled.simulation.audio_paused(false);
        disabled.scene.entity("speaker").set<LocalTranslation>({0, 0, 10 - 33 * dt});
        disabled.tick();
        check(std::abs(audio_frequency(disabled) - 440) < .5,
              "Legacy/default or nonspatial sound gained Doppler pitch");
    }
    // Native radial denominator remains positive for an extreme finite movement.
    f.load(document);
    actor = f.scene.entity("speaker");
    actor.set<LocalTranslation>({0, 0, 1e9});
    f.simulation.tick(1e-12f);
    check(std::abs(audio_frequency(f) - 440 * 2 / 3) < .5,
          "Extreme movement failed finite native Doppler saturation");
    // Real physics motion and teleport propagation through the effective parent graph.
    Fixture linked(root, 1, true);
    linked.load(document);
    const auto parent =
        authoring_command(linked.scene, "entity.create", {{"name", "Moving parent"}})
            .at("selected")
            .get<std::string>();
    authoring_command(linked.scene, "component.add",
                      {{"entity", parent}, {"component", "forge.physics_body"}});
    authoring_command(linked.scene, "component.add",
                      {{"entity", parent}, {"component", "forge.box_collider"}});
    auto body = linked.scene.entity(parent).get<PhysicsBody>();
    body.motion = 1;
    linked.scene.entity(parent).set(body);
    linked.scene.reparent_entity("speaker", parent, ReparentMode::KeepLocal);
    linked.simulation.audio_paused(false);
    linked.tick();
    const auto physics =
        std::static_pointer_cast<PhysicsRuntime>(linked.engine.services().physics());
    const auto parent_ref =
        linked.engine.world().reference(linked.scene.entity(parent).id()).value();
    physics->move_kinematic(parent_ref, {0, 0, -33 * dt}, {});
    linked.tick();
    check(std::abs(audio_frequency(linked) - 440 * sound_speed / (sound_speed - 33)) < .5,
          "Child emitter ignored effective kinematic parent velocity");
    physics->teleport(parent_ref, {0, 0, -5}, {}, true);
    linked.tick();
    check(std::abs(audio_frequency(linked) - 440) < .5,
          "Physics teleport failed to reset dependent child emitter motion");
}
