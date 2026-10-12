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
