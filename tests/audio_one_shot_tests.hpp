#pragma once
#include "audio_source_control_tests.hpp"
void audio_one_shot_tests(const std::filesystem::path& root, AssetId clip) {
    auto shot = [](Fixture& f) {
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::OneShot);
    };
    {
        Fixture single(root), layered(root);
        single.load(source(clip));
        layered.load(source(clip));
        shot(layered);
        shot(layered);
        check(layered.audio->status().at("voices") == 3 &&
                  layered.audio->status().at("clips") == 1 &&
                  layered.audio->status().at("decoded_bytes") ==
                      single.audio->status().at("decoded_bytes"),
              "Overlapping playback duplicated PCM or replaced a voice");
        single.audio->paused(false);
        layered.audio->paused(false);
        (void)single.energy(4096);
        (void)layered.energy(4096);
        const auto ratio = layered.energy(4096) / single.energy(4096);
        check(ratio > 8.8 && ratio < 9.2, "Three in-phase voices did not mix additively");
        std::cout << "Three-voice energy ratio=" << ratio << "\n";
    }
    {
        const auto intro_root = root / "shot-intro";
        std::filesystem::create_directories(intro_root);
        wav(intro_root / "intro.wav", 1, 48000, true);
        auto intro = AssetCatalog::register_audio_clip(intro_root, "intro.wav").id;
        Fixture f(intro_root);
        f.load(source(intro));
        f.audio->paused(false);
        (void)f.energy(24000);
        shot(f);
        check(f.energy(4096) > 1e-5, "One-shot restarted the main sound's silent intro");
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Stop);
        shot(f);
        (void)f.energy(24000);
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Pause);
        check(AudioSourceAccess::status(*f.audio, f.ref()).at("paused_one_shots") == 1,
              "Stopped primary prevented effect Pause");
        (void)f.energy(4096);
        check(f.energy(48000) == 0 && f.audio->status().at("one_shots") == 1,
              "Paused effect continued or retired");
        f.audio->paused(true);
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Resume);
        check(f.energy(4096) == 0, "Effect Resume bypassed whole-game pause");
        f.audio->paused(false);
        check(f.energy(4096) > 1e-5 &&
                  !AudioSourceAccess::status(*f.audio, f.ref()).at("playing").get<bool>(),
              "Effect Resume restarted the intro or the stopped primary");
        (void)f.energy(96000);
        check(f.audio->status().at("one_shots") == 0 && f.energy(4096) == 0,
              "Effect looped or failed to retire naturally");
    }
    {
        Fixture f(root);
        f.load(source(clip));
        const auto authored = f.scene.snapshot();
        const auto tick = f.audio->status().at("tick");
        for (unsigned i = 0; i < 17; ++i)
            shot(f);
        check(f.audio->status().at("one_shots") == 16 && f.audio->status().at("voices") == 17 &&
                  f.audio->status().at("failed_sources") == 0,
              "Per-source effect cap failed or rejected the primary");
        check(f.scene.snapshot() == authored && f.audio->status().at("tick") == tick,
              "Effect trigger changed authored scene or simulation time");
        f.audio->paused(false);
        f.audio->mix_volumes(0, {1, 1, 1});
        check(f.energy(4096) == 0, "Master mute missed overlapping voices");
        f.audio->mix_volumes(1, {1, 1, 1});
        check(f.energy(4096) > 1e-5, "Effects did not resume after Master mute");
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Stop);
        (void)f.energy(4096);
        check(f.audio->status().at("voices") == 1 && f.audio->status().at("one_shots") == 0 &&
                  f.energy(4096) == 0,
              "Source Stop did not retire every effect");
        f.audio->play_one_shot(f.ref());
        check(f.audio->status().at("one_shots") == 0, "Gameplay trigger bypassed owner queue");
        f.tick();
        check(f.audio->status().at("one_shots") == 1, "Queued gameplay effect did not flush");
        f.scene.delete_subtree("speaker");
        f.audio->poll_loading();
        check(f.audio->status().at("one_shots") == 0,
              "Host poll retained a deleted emitter's effect");
    }
    {
        Fixture f(root);
        auto doc = source(clip);
        doc["entities"][0]["components"]["forge.audio_source"]["play_on_start"] = false;
        f.scene.restore_snapshot(doc);
        f.audio->play_one_shot(f.ref());
        f.audio->stop_source(f.ref());
        f.audio->play_one_shot(f.ref());
        f.audio->pause_source(f.ref());
        f.simulation.sync_audio();
        check(f.audio->status().at("pending_sources") == 1,
              "Pending effect fixture did not load asynchronously");
        check(f.audio->wait_for_sources(std::chrono::seconds(5)), "Pending effect clip failed");
        auto state = AudioSourceAccess::status(*f.audio, f.ref());
        check(state.at("one_shots") == 1 && state.at("paused_one_shots") == 1 &&
                  !state.at("playing").get<bool>(),
              "Ordered pending Stop/trigger/Pause lost effect intent or leaked primary autoplay");
        f.audio->paused(false);
        check(f.energy(4096) == 0, "Pending paused effect leaked output");
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Resume);
        check(f.energy(4096) > 1e-5, "Pending effect could not Resume");
    }
    {
        Fixture f(root);
        auto doc = source(clip);
        for (unsigned i = 1; i < 256; ++i) {
            auto peer = doc["entities"][0];
            peer["id"] = "peer" + std::to_string(i);
            doc["entities"].push_back(peer);
        }
        f.load(doc);
        shot(f);
        check(f.audio->status().at("voices") == 256 && f.audio->status().at("one_shots") == 0 &&
                  f.audio->status().at("failed_sources") == 0,
              "Global effect cap displaced a primary or exceeded total voice budget");
    }
    {
        Fixture f(root);
        auto doc = source(clip);
        doc["entities"][0]["components"]["forge.audio_source"]["play_on_start"] = false;
        f.load(doc);
        const auto actor = f.scene.entity("speaker");
        const auto native = actor.id();
        f.audio->play_one_shot(f.ref());
        f.scene.delete_subtree("speaker");
        f.scene.restore_snapshot(doc);
        check(f.scene.entity("speaker").id() != native,
              "Reconstruction did not replace native generation");
        f.audio->synchronize(1);
        check(f.audio->status().at("one_shots") == 0, "Stale trigger reached reconstructed source");
        shot(f);
        f.scene.entity("speaker").remove<AudioSource>();
        f.audio->poll_loading();
        check(f.audio->status().at("one_shots") == 0, "Removed component retained effect");
    }
    {
        wav(root / "Assets/one-shot-alternate.wav");
        const auto alternate =
            AssetCatalog::register_audio_clip(root, "Assets/one-shot-alternate.wav").id;
        Fixture f(root);
        f.load(source(clip));
        shot(f);
        f.audio->play_one_shot(f.ref());
        auto value = f.scene.entity("speaker").get<AudioSource>();
        value.clip.id = alternate;
        f.scene.entity("speaker").set(value);
        f.audio->synchronize(1);
        check(f.audio->status().at("one_shots") == 0, "Clip switch retained old PCM effect");
        check(f.audio->wait_for_sources(std::chrono::seconds(5)) &&
                  f.audio->status().at("one_shots") == 0,
              "Stale clip command created a new effect after loading");
    }
    {
        Fixture f(root);
        auto doc = source(clip);
        auto& values = doc["entities"][0]["components"]["forge.audio_source"];
        values["play_on_start"] = false;
        values["spatialized"] = true;
        values["minimum_distance"] = 2;
        values["maximum_distance"] = 10;
        values["attenuation"] = 1;
        values["pitch"] = .25;
        doc["entities"][0]["components"]["forge.position"]["z"] = 2;
        doc["entities"].push_back({{"id", "listener"},
                                   {"name", "Listener"},
                                   {"components",
                                    {{"forge.position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                                     {"forge.audio_listener", {{"enabled", true}}}}}});
        f.load(doc);
        f.audio->paused(false);
        shot(f);
        (void)f.energy(4096);
        const auto nominal = f.energy(4800);
        check(nominal > 1e-5, "Spatial effect reference is silent");
        f.scene.entity("speaker").set<LocalTranslation>({0, 0, 6});
        f.tick();
        (void)f.energy(4096);
        check(std::abs(f.energy(4800) / nominal - .25) < .005,
              "Effect did not follow moved source's linear falloff");
        auto value = f.scene.entity("speaker").get<AudioSource>();
        value.bus = 2;
        value.gain = .5f;
        f.scene.entity("speaker").set(value);
        f.audio->mix_volumes(1, {1, .5f, 1});
        f.tick();
        (void)f.energy(4096);
        check(std::abs(f.energy(4800) / nominal - .015625) < .002,
              "Effect missed changed source gain or Effects bus");
        value.gain = 1;
        value.bus = 0;
        value.doppler = 1;
        value.rolloff = 0;
        f.scene.entity("speaker").set(value);
        f.tick();
        AudioSourceAccess::control(*f.audio, f.ref(), AudioSourceOperation::Stop);
        constexpr float dt = 1.f / 60;
        f.scene.entity("speaker").set<LocalTranslation>({0, 0, 6 - 33 * dt});
        f.tick();
        shot(f);
        check(std::abs(audio_frequency(f) - 110 * 343.3 / (343.3 - 33)) < .5,
              "New effect did not inherit last fixed source velocity and pitch");
        const std::array discontinuities{f.scene.entity("speaker").id()};
        f.audio->synchronize(f.audio->status().at("tick").get<std::uint64_t>(), dt,
                             discontinuities);
        check(std::abs(audio_frequency(f) - 110) < .5,
              "Effect retained Doppler across an explicit motion discontinuity");
    }
    {
        Fixture f(root);
        f.scene.restore_snapshot(source(clip));
        for (unsigned i = 0; i < 16; ++i)
            f.audio->play_one_shot(f.ref());
        reject([&] { f.audio->play_one_shot(f.ref()); });
        f.audio->stop_source(f.ref());
        f.audio->play_one_shot(f.ref());
        f.simulation.sync_audio();
        check(f.audio->wait_for_sources(std::chrono::seconds(5)) &&
                  f.audio->status().at("one_shots") == 1,
              "Bounded pending triggers or ordered Stop replacement lost final effect");
        f.audio->shutdown();
        reject([&] { f.audio->play_one_shot(f.ref()); });
    }
}
