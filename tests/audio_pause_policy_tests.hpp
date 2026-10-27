#pragma once
#include "../src/audio_preview_access.hpp"
#include <forge/game_session.hpp>

void audio_mixed_pause_tests(const std::filesystem::path& root, AssetId clip) {
    Fixture f(root);
    auto doc = source(clip);
    auto music = doc["entities"][0];
    music["id"] = "music";
    music["name"] = "Independent music";
    music["components"]["forge.audio_source"]["pause_with_game"] = false;
    music["components"]["forge.audio_source"]["pitch"] = 2.f;
    doc["entities"].push_back(music);
    f.load(doc);
    f.simulation.activate_audio(false);
    auto spectrum = [&] {
        std::vector<float> pcm(9600);
        f.audio->read_offline(pcm);
        std::array<double, 2> power{};
        for (unsigned tone = 0; tone < 2; ++tone) {
            double sine = 0, cosine = 0;
            for (std::size_t i = 0; i < pcm.size() / 2; ++i) {
                const double phase = 2 * 3.141592653589793 * (tone ? 880 : 440) * i / 48000;
                sine += pcm[i * 2] * std::sin(phase);
                cosine += pcm[i * 2] * std::cos(phase);
            }
            power[tone] = sine * sine + cosine * cosine;
        }
        return power;
    };
    (void)f.energy(4096);
    const auto running = spectrum();
    check(running[0] > 100 && running[1] > 100, "Mixed pause reference lacks both tones");
    f.simulation.audio_paused(true);
    (void)f.energy(4096); // Native graph/device boundaries may retain a prior mix quantum.
    const auto paused = spectrum();
    check(paused[0] / running[0] < .0001 && std::abs(paused[1] / running[1] - 1) < .01,
          "Gameplay pause did not separate ordinary sound from independent music");
    check(AudioSourceAccess::status(*f.audio, f.ref()).at("paused") &&
              !AudioSourceAccess::status(*f.audio, f.ref("music")).at("paused"),
          "Mixed source observations do not reflect their own pause policy");
    f.simulation.audio_paused(false);
    (void)f.energy(4096);
    const auto resumed = spectrum();
    check(std::abs(resumed[0] / running[0] - 1) < .01 &&
              std::abs(resumed[1] / running[1] - 1) < .01,
          "Resume changed mixed source gain or lost either sound");
    f.simulation.suspend_audio();
    check(f.energy(4096) == 0, "Full suspension leaked a mixed source");
}

void audio_pause_lifecycle_tests(const std::filesystem::path& root, AssetId clip) {
    Fixture authored(root);
    auto doc = source(clip);
    doc["entities"][0]["components"]["forge.audio_source"]["pause_with_game"] = false;
    authored.load(doc);
    const auto snapshot = authored.scene.snapshot();
    bool fail_control = false;
    std::shared_ptr<AudioRuntime> candidate_audio;
    struct Observe final : GameScenePreparation {
        std::shared_ptr<AudioRuntime>& audio;
        explicit Observe(std::shared_ptr<AudioRuntime>& target) : audio(target) {}
        GamePreparationProgress poll(RuntimeWorld& world) override {
            audio = std::static_pointer_cast<AudioRuntime>(world.engine.services().audio());
            std::vector<float> samples(1024);
            audio->read_offline(samples);
            check(std::ranges::all_of(samples, [](float x) { return x == 0; }),
                  "Unpublished independent music escaped preparation");
            return {true, "ready", 1, 1};
        }
        void activate() noexcept override {}
    };
    EngineModule module;
    module.id = "test.pause_fault";
    module.runtime_roles = role_mask(WorldRole::Runtime);
    module.controls = [&](ModuleContext&) {
        if (fail_control)
            throw std::runtime_error("Original control failure");
    };
    GameSessionConfig config;
    config.audio = AudioConfig{root, AudioOutput::Offline, true};
    config.modules = {module};
    config.preparation = [&](std::uint64_t) { return std::make_unique<Observe>(candidate_audio); };
    GameSession session(config);
    auto prepare = [&] {
        const auto ticket = session.prepare(snapshot);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!session.poll_preparation(ticket).ready) {
            check(std::chrono::steady_clock::now() < deadline, "Audio preparation timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return ticket;
    };
    auto energy = [](const std::shared_ptr<AudioRuntime>& audio) {
        std::vector<float> samples(4096);
        audio->read_offline(samples);
        double sum = 0;
        for (float sample : samples)
            sum += double(sample) * sample;
        return sum / samples.size();
    };
    const auto now = RuntimeClock::Clock::now();
    auto ticket = prepare();
    auto old = candidate_audio;
    session.activate(ticket, now, false);
    check(energy(old) > 1e-5 && session.status().at("state") == "paused",
          "Published paused scene did not activate independent music");
    ticket = prepare();
    auto cancelled = candidate_audio;
    session.cancel(ticket);
    reject([&] { energy(cancelled); });
    check(energy(old) > 1e-5, "Candidate cancellation stopped active independent music");
    ticket = prepare();
    auto current = candidate_audio;
    session.activate(ticket, now, false);
    reject([&] { energy(old); });
    check(energy(current) > 1e-5, "Scene replacement lost independent music");
    const EntityRef live_ref{snapshot.at("asset_id").get<AssetId>(),
                             snapshot.at("entities")[0].at("id").get<EntityId>()};
    auto& active_world = session.active();
    active_world.tune_property(live_ref, "forge.audio_source", "pause_with_game", true);
    check(energy(current) == 0 && current->status().at("tick") == 0,
          "Live runtime owner did not apply pause policy without a fixed tick");
    active_world.tune_property(live_ref, "forge.audio_source", "pause_with_game", false);
    check(energy(current) > 1e-5 && current->status().at("tick") == 0,
          "Live runtime owner did not restore independent music while paused");
    const auto before_bad = active_world.scene.snapshot();
    reject([&] {
        active_world.tune_property(live_ref, "forge.audio_source", "pause_with_game", "false");
    });
    check(active_world.scene.snapshot() == before_bad && energy(current) > 1e-5,
          "Invalid live pause policy changed the admitted world/audio");
    const auto dry_energy = energy(current);
    active_world.tune_property(live_ref, "forge.audio_source", "low_pass_hz", 100.f);
    active_world.tune_property(live_ref, "forge.audio_source", "low_pass_enabled", true);
    (void)energy(current);
    check(energy(current) < dry_energy * .01 && current->status().at("tick") == 0,
          "RuntimeWorld did not apply the live filter without a fixed tick");
    const auto filter_snapshot = active_world.scene.snapshot();
    reject([&] {
        active_world.tune_property(live_ref, "forge.audio_source", "high_pass_hz", 20001.f);
    });
    check(active_world.scene.snapshot() == filter_snapshot,
          "Invalid live filter changed the admitted world");
    fail_control = true;
    reject([&] { session.control_frame(); });
    check(session.status().at("state") == "faulted" &&
              session.status().at("error").get<std::string>().find("Original control failure") !=
                  std::string::npos &&
              energy(current) == 0,
          "Faulted session lost its error or left independent music audible");
    session.unload(now);
    reject([&] { energy(current); });
}

void audio_paused_device_tests(const std::filesystem::path& root, AssetId clip) {
    EngineContext engine(WorldRole::Runtime, false,
                         {audio_module({root, AudioOutput::NullDeviceTest, true})});
    Scene scene(engine.world());
    auto doc = source(clip);
    auto& config = doc["entities"][0]["components"]["forge.audio_source"];
    config["pause_with_game"] = false;
    config["play_on_start"] = false;
    config["pitch"] = 4.f;
    config["low_pass_enabled"] = true;
    config["low_pass_hz"] = 1000.f;
    scene.restore_snapshot(doc);
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.sync_audio();
    auto audio = std::static_pointer_cast<AudioRuntime>(engine.services().audio());
    check(audio->wait_for_sources(std::chrono::seconds(5)), "Null-device preparation failed");
    simulation.activate_audio(true);
    const auto ref = engine.world().reference(scene.entity("speaker").id()).value();
    audio->control_unpaused_source(ref, AudioSourceOperation::OneShot);
    check(audio->status().at("one_shots") == 1, "Null-device UI one-shot was not created");
    const auto initial = audio->status();
    for (unsigned i = 0; i < 40; ++i) {
        auto value = scene.entity("speaker").get<AudioSource>();
        value.low_pass_enabled = i % 3 != 0;
        value.high_pass_enabled = i % 2 != 0;
        value.low_pass_hz = i % 2 ? 400.f : 4000.f;
        value.high_pass_hz = i % 2 ? 20.f : 200.f;
        scene.entity("speaker").set(value);
        audio->refresh_sources();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (audio->status().at("one_shots") != 0) {
        audio->poll_loading();
        check(std::chrono::steady_clock::now() < deadline,
              "Paused UI one-shot was not retired by the real callback/loading owner");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(audio->status().at("paused") && audio->status().at("tick") == 0 &&
              audio->status().at("mixed_frames") > initial.at("mixed_frames"),
          "Device-driven paused effect advanced gameplay or skipped native mixing");
}

void audio_pause_policy_tests(const std::filesystem::path& root, AssetId clip) {
    Fixture f(root);
    f.load(source(clip));
    auto actor = f.scene.entity("speaker");
    auto config = actor.get<AudioSource>();
    check(config.pause_with_game, "Old authored sound did not default to gameplay pause");
    check(f.energy(4096) == 0, "Prepared world leaked audio before activation");
    reject([&] { f.audio->control_unpaused_source(f.ref(), AudioSourceOperation::Restart); });
    f.simulation.activate_audio(true);
    check(f.audio->status().at("paused") && !f.audio->status().at("suspended") &&
              f.energy(4096) == 0,
          "Paused activation played an ordinary gameplay source");
    f.simulation.audio_paused(false);
    check(f.energy(4096) > 1e-5, "Ordinary source did not play after Resume");
    f.simulation.audio_paused(true);
    (void)f.energy(4096);
    const auto held = AudioPreviewAccess::elapsed(*f.audio, f.ref());
    check(f.energy(4800) < 1e-12 && AudioPreviewAccess::elapsed(*f.audio, f.ref()) == held,
          "Gameplay pause advanced an ordinary sound cursor");
    config.pause_with_game = false;
    actor.set(config);
    f.simulation.sync_audio();
    const auto tick = f.audio->status().at("tick");
    check(f.energy(4800) > 1e-5 && AudioPreviewAccess::elapsed(*f.audio, f.ref()) > held &&
              f.audio->status().at("paused") && !f.audio->status().at("suspended") &&
              f.audio->status().at("tick") == tick,
          "Unpaused sound did not use the device clock independently of gameplay");
    const auto before = f.scene.snapshot();
    f.audio->control_unpaused_source(f.ref(), AudioSourceOperation::Pause);
    (void)f.energy(4096);
    const auto manually_held = AudioPreviewAccess::elapsed(*f.audio, f.ref());
    check(f.energy(4800) < 1e-12 && AudioPreviewAccess::elapsed(*f.audio, f.ref()) == manually_held,
          "Explicit source Pause did not freeze independent music");
    f.audio->control_unpaused_source(f.ref(), AudioSourceOperation::Resume);
    check(f.energy(4800) > 1e-5, "Control-domain Resume did not continue independent music");
    f.audio->control_unpaused_source(f.ref(), AudioSourceOperation::OneShot);
    check(f.audio->status().at("one_shots") == 1 && f.scene.snapshot() == before &&
              f.audio->status().at("tick") == tick,
          "Paused UI one-shot changed gameplay/authored state or failed to queue");
    (void)f.energy(96000);
    f.audio->poll_loading();
    check(f.audio->status().at("one_shots") == 0,
          "Finished independent UI one-shot retained a voice while gameplay was paused");
    f.audio->mix_volumes(0, {1, 1, 1});
    (void)f.energy(4096);
    check(f.energy(4800) < 1e-12, "Master mute did not include independent music");
    f.audio->mix_volumes(1, {1, 1, 1});
    f.simulation.suspend_audio();
    (void)f.energy(4096);
    check(f.energy(4800) < 1e-12 && f.audio->status().at("suspended"),
          "Full lifecycle suspension did not silence independent music");
    reject([&] { f.audio->control_unpaused_source(f.ref(), AudioSourceOperation::Restart); });
    f.simulation.activate_audio(true);
    check(f.energy(4800) > 1e-5, "Reactivation lost independent music playback");
    config.pause_with_game = true;
    actor.set(config);
    f.simulation.sync_audio();
    (void)f.energy(4096);
    check(f.energy(4800) < 1e-12, "Routing back to gameplay audio ignored the active pause");
    reject([&] { f.audio->control_unpaused_source(f.ref(), AudioSourceOperation::OneShot); });
    const EntityRef stale{f.ref().scene, EntityId::generate()};
    reject([&] { f.audio->control_unpaused_source(stale, AudioSourceOperation::Restart); });
    reject(
        [&] { f.audio->control_unpaused_source(f.ref(), static_cast<AudioSourceOperation>(99)); });
    audio_mixed_pause_tests(root, clip);
    audio_pause_lifecycle_tests(root, clip);
    audio_paused_device_tests(root, clip);
}
