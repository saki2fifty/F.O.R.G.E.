#pragma once
#include "../src/audio_source_access.hpp"
void audio_source_control_tests(const std::filesystem::path& root, AssetId clip) {
    Fixture f(root);
    f.load(source(clip));
    const auto ref = f.ref();
    const auto before = f.scene.snapshot();
    const auto tick = f.audio->status().at("tick");
    check(AudioSourceAccess::status(*f.audio, ref).at("playing") == true,
          "Autoplay source observation is wrong");
    AudioSourceAccess::control(*f.audio, ref, false);
    check(AudioSourceAccess::status(*f.audio, ref).at("playing") == false && f.energy() == 0,
          "Immediate Stop failed at paused owner boundary");
    AudioSourceAccess::control(*f.audio, ref, true);
    check(AudioSourceAccess::status(*f.audio, ref).at("playing") == true && f.energy() == 0,
          "Restart advanced output while gameplay was paused");
    check(f.audio->status().at("tick") == tick && f.scene.snapshot() == before &&
              f.audio->status().at("voices") == 1 && f.audio->status().at("clips") == 1,
          "Immediate control changed simulation, authored state or ownership");
    const EntityRef stale{ref.scene, EntityId::generate()};
    reject([&] { AudioSourceAccess::control(*f.audio, stale, false); });
    check(AudioSourceAccess::status(*f.audio, ref).at("playing") == true,
          "Stale source control stopped the admitted source");
    f.simulation.audio_paused(false);
    check(f.energy() > 1e-5, "Restart did not begin sound after Resume");
    AudioSourceAccess::control(*f.audio, ref, false);
    (void)f.energy(4096); // Drain the already-rendered native graph quantum.
    check(f.energy() == 0, "Stop continued source output");
    AudioSourceAccess::control(*f.audio, ref, true);
    check(f.energy() > 1e-5, "Manual trigger failed without a simulation tick");
}
void audio_source_pause_tests(const std::filesystem::path& root) {
    const auto marker_root = root / "pause-marker";
    std::filesystem::create_directories(marker_root);
    wav(marker_root / "marker.wav", 1, 48000, true);
    const auto clip = AssetCatalog::register_audio_clip(marker_root, "marker.wav").id;
    auto doc = source(clip);
    doc["entities"][0]["components"]["forge.audio_source"]["loop"] = false;
    Fixture f(marker_root);
    f.load(doc);
    const auto ref = f.ref();
    const auto authored = f.scene.snapshot();
    const auto tick = f.audio->status().at("tick");
    auto state = [&] { return AudioSourceAccess::status(*f.audio, ref); };
    auto control = [&](AudioSourceOperation op) { AudioSourceAccess::control(*f.audio, ref, op); };
    check(!state().at("source_paused").get<bool>(), "New voice inherited source pause");
    f.simulation.audio_paused(false);
    check(f.energy(1024) == 0, "Original marker intro is not silent");
    check(f.energy(24000) > 1e-5, "Original marker never reached its tone");
    control(AudioSourceOperation::Pause);
    check(state().at("source_paused") == true && state().at("playing") == false,
          "Pause did not stop the source while retaining resume intent");
    (void)f.energy(4096); // Submitted native graph frames can finish.
    check(f.energy(48000) == 0, "Source pause continued mixing");
    control(AudioSourceOperation::Pause);
    control(AudioSourceOperation::Resume);
    check(state().at("playing") == true && state().at("source_paused") == false &&
              f.energy() > 1e-5,
          "Resume restarted the silent intro or lost its retained position");
    control(AudioSourceOperation::Resume); // Already running: no restart.
    check(f.energy() > 1e-5, "Repeated Resume restarted a running source");
    control(AudioSourceOperation::Restart);
    (void)f.energy(4096);
    check(f.energy() == 0, "Restart failed to return to the silent intro");
    (void)f.energy(96000);
    control(AudioSourceOperation::Pause);
    control(AudioSourceOperation::Resume);
    check(state().at("playing") == false && state().at("source_paused") == false && f.energy() == 0,
          "Pause/Resume restarted a naturally completed nonloop source");
    control(AudioSourceOperation::Restart);
    control(AudioSourceOperation::Pause);
    control(AudioSourceOperation::Stop);
    control(AudioSourceOperation::Resume);
    check(state().at("playing") == false && state().at("source_paused") == false,
          "Stop retained source pause or Resume restarted a stopped source");
    control(AudioSourceOperation::Restart);
    control(AudioSourceOperation::Pause);
    f.simulation.audio_paused(true);
    control(AudioSourceOperation::Resume);
    check(state().at("playing") == true && state().at("paused") == true && f.energy() == 0,
          "Source Resume bypassed whole-game pause");
    f.simulation.audio_paused(false);
    control(AudioSourceOperation::Pause);
    f.simulation.audio_paused(true);
    f.simulation.audio_paused(false);
    check(state().at("source_paused") == true && !state().at("playing").get<bool>(),
          "Whole-game Resume resumed an explicitly paused source");
    reject([&] { control(AudioSourceOperation(99)); });
    check(state().at("source_paused") == true && f.scene.snapshot() == authored &&
              f.audio->status().at("tick") == tick && f.audio->status().at("voices") == 1 &&
              f.audio->status().at("clips") == 1,
          "Source operations changed authoring, time or voice/cache ownership");

    // Public gameplay service commands remain queued until the fixed owner boundary.
    f.audio->resume_source(ref);
    check(state().at("source_paused") == true, "Gameplay Resume bypassed its queue");
    f.tick();
    check(state().at("playing") == true, "Queued gameplay Resume did not flush");
    f.audio->pause_source(ref);
    f.tick();
    check(state().at("source_paused") == true, "Queued gameplay Pause did not flush");

    auto multiple = source(clip);
    auto second = multiple["entities"][0];
    second["id"] = "other";
    multiple["entities"].push_back(second);
    Fixture paired(marker_root);
    paired.load(multiple);
    paired.simulation.audio_paused(false);
    (void)paired.energy(24000);
    AudioSourceAccess::control(*paired.audio, paired.ref(), AudioSourceOperation::Pause);
    (void)paired.energy(4096);
    check(paired.audio->status().at("playing_sources") == 1 && paired.energy() > 1e-5 &&
              paired.audio->status().at("voices") == 2 && paired.audio->status().at("clips") == 1,
          "Pausing one loop source affected its peer or duplicated the clip");
    AudioSourceAccess::control(*paired.audio, paired.ref(), AudioSourceOperation::Resume);
    check(paired.audio->status().at("playing_sources") == 2,
          "Loop source failed to resume beside its peer");
}
