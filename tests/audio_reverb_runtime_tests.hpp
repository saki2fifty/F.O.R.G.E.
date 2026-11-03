#pragma once
#include "audio_source_access.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

void audio_reverb_runtime_tests(const std::filesystem::path& root, AssetId clip) {
    auto pcm = [](Fixture& fixture, unsigned frames) {
        std::vector<float> samples(frames * 2);
        fixture.audio->read_offline(samples);
        for (float sample : samples)
            check(std::isfinite(sample), "Native reverb produced nonfinite PCM");
        return samples;
    };
    auto configure = [&](Fixture& fixture, bool enabled, float wet, bool chain = false) {
        auto doc = source(clip);
        auto& config = doc["entities"][0]["components"]["forge.audio_source"];
        config["reverb_enabled"] = enabled;
        config["reverb_wet"] = wet;
        config["reverb_decay"] = .3;
        config["reverb_damping"] = .4;
        config["low_pass_enabled"] = chain;
        config["high_pass_enabled"] = chain;
        config["echo_enabled"] = chain;
        config["echo_seconds"] = .03125;
        config["echo_feedback"] = .5;
        fixture.load(doc);
        fixture.simulation.activate_audio(false);
    };
    Fixture dry(root), dormant(root), zero(root), wet(root), chain(root);
    configure(dry, false, .25f);
    configure(dormant, false, 1);
    configure(zero, true, 0);
    configure(wet, true, 1);
    configure(chain, true, .4f, true);
    const auto reference = pcm(dry, 8000);
    check(std::any_of(reference.begin(), reference.end(), [](float value) { return value != 0; }),
          "Native reverb reference source is silent or its world is suspended");
    check(pcm(dormant, 8000) == reference && pcm(zero, 8000) == reference,
          "Disabled or zero-wet reverb changed dry PCM");
    const auto reverberated = pcm(wet, 8000);
    check(std::all_of(reverberated.begin(), reverberated.begin() + 2000,
                      [](float value) { return value == 0; }) &&
              reverberated != reference,
          "Wet-only node bypassed diffusion latency or did not process audio");
    check(wet.energy(16000) > 1e-8 && chain.energy(16000) > 1e-8,
          "Native reverb or HPF/LPF/echo/reverb chain is silent");
    check(dry.audio->status().at("reverb_buffer_bytes") == 0 &&
              zero.audio->status().at("reverb_buffer_bytes") == 0 &&
              wet.audio->status().at("reverb_buffer_bytes").get<unsigned>() > 50000 &&
              wet.audio->status().at("echo_buffer_bytes") == 0 &&
              chain.audio->status().at("echo_buffer_bytes") == 12000,
          "Derived reverb memory allocation or echo accounting changed");

    Fixture uninterrupted(root);
    configure(uninterrupted, true, 1);
    auto reference_entity = uninterrupted.scene.entity("speaker");
    auto reference_settings = reference_entity.get<AudioSource>();
    reference_settings.bus = 2;
    reference_entity.set(reference_settings);
    uninterrupted.simulation.sync_audio();
    (void)pcm(uninterrupted, 24000);
    auto wet_entity = wet.scene.entity("speaker");
    auto wet_settings = wet_entity.get<AudioSource>();
    wet_settings.bus = 2;
    wet_entity.set(wet_settings);
    wet.simulation.sync_audio();
    wet.audio->mix_volumes(1, {1, 0, 1});
    const auto channel_muted = pcm(wet, 6000);
    (void)pcm(uninterrupted, 6000);
    check(std::all_of(channel_muted.begin() + 4, channel_muted.end(),
                      [](float value) { return value == 0; }),
          "Muted Effects channel leaked buffered source reverb");
    wet.audio->mix_volumes(1, {1, 1, 1});
    const auto unmuted = pcm(wet, 12000), continuing = pcm(uninterrupted, 12000);
    check(std::equal(unmuted.begin() + 4, unmuted.end(), continuing.begin() + 4),
          "Category mute stopped or reset source/DSP time");
    wet_settings.gain = 0;
    wet_entity.set(wet_settings);
    wet.simulation.sync_audio();
    const auto gain_muted = pcm(wet, 6000);
    (void)pcm(uninterrupted, 6000);
    check(std::all_of(gain_muted.begin() + 4, gain_muted.end(),
                      [](float value) { return value == 0; }),
          "Zero source gain leaked a buffered reverb tail");
    wet_settings.gain = 1;
    wet_entity.set(wet_settings);
    wet.simulation.sync_audio();
    const auto restored_gain = pcm(wet, 12000), gain_reference = pcm(uninterrupted, 12000);
    check(std::equal(restored_gain.begin() + 4, restored_gain.end(), gain_reference.begin() + 4),
          "Source gain edits stopped or reset source/DSP time");

    Fixture tail(root);
    auto doc = source(clip);
    auto& config = doc["entities"][0]["components"]["forge.audio_source"];
    config["loop"] = false;
    config["play_on_start"] = false;
    config["reverb_enabled"] = true;
    config["reverb_decay"] = .3;
    config["reverb_wet"] = .5;
    tail.load(doc);
    tail.simulation.activate_audio(false);
    tail.audio->play_one_shot(tail.ref());
    tail.audio->refresh_sources();
    (void)pcm(tail, 50000);
    tail.audio->refresh_sources();
    auto status = AudioSourceAccess::status(*tail.audio, tail.ref());
    check(status.at("one_shots") == 1 && status.at("reverb_tails") == 1 &&
              status.at("echo_tails") == 0,
          "Reverb one-shot retired at clip EOS or appeared as an echo");
    tail.simulation.audio_paused(true);
    check(tail.energy(96000) == 0 && tail.audio->status().at("one_shots") == 1,
          "Game pause advanced or leaked a post-EOS reverb tail");
    tail.simulation.audio_paused(false);
    tail.audio->pause_source(tail.ref());
    tail.audio->refresh_sources();
    // The existing pausable group resamples even at pitch1. A leaf control
    // cannot revoke its two already buffered output frames without disturbing
    // other voices. Require silence after that fixed native pipeline boundary,
    // then prove sustained silence and retained ownership while still paused.
    const auto paused_pcm = pcm(tail, 96000);
    check(std::all_of(paused_pcm.begin() + 4, paused_pcm.end(),
                      [](float value) { return value == 0; }) &&
              tail.audio->status().at("one_shots") == 1 &&
              AudioSourceAccess::status(*tail.audio, tail.ref()).at("paused_one_shots") == 1 &&
              tail.energy(1024) == 0,
          "Source pause leaked beyond two buffered frames or retired its reverb tail");
    tail.audio->resume_source(tail.ref());
    tail.audio->refresh_sources();
    check(tail.energy(16000) > 1e-10, "Resumed source lost its pending reverb tail");
    (void)pcm(tail, 60000);
    tail.audio->refresh_sources();
    check(tail.audio->status().at("one_shots") == 0,
          "Reverb one-shot did not retire after its bounded tail");
    wet.audio->stop_source(wet.ref());
    wet.audio->refresh_sources();
    const auto stopped = pcm(wet, 20000);
    check(std::all_of(stopped.begin() + 4, stopped.end(), [](float value) { return value == 0; }) &&
              wet.energy(1024) == 0,
          "Stop left an active reverb tail beyond the native pipeline boundary");
    wet.audio->play(wet.ref());
    wet.audio->refresh_sources();
    const auto restart = pcm(wet, 1000);
    check(std::all_of(restart.begin(), restart.end(), [](float value) { return value == 0; }),
          "Restart retained old reverb history");
    EngineContext author;
    Scene scene(author.world());
    const auto actor = authoring_command(scene, "entity.create").at("selected").get<std::string>();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.audio_source"}});
    const auto defaults = scene.entity(actor).get<AudioSource>();
    check(!defaults.reverb_enabled && defaults.reverb_decay == 1.5f &&
              defaults.reverb_damping == .5f && defaults.reverb_wet == .25f,
          "Legacy/default audio source did not keep reverb disabled");
    const auto before = scene.snapshot();
    authoring_command(scene, "property.set_many",
                      {{"entity", actor},
                       {"component", "forge.audio_source"},
                       {"values",
                        {{"reverb_enabled", true},
                         {"reverb_decay", .1},
                         {"reverb_damping", .95},
                         {"reverb_wet", 1}}}});
    const auto after = scene.snapshot();
    check(scene.entity(actor).get<AudioSource>().reverb_enabled && scene.undo() &&
              scene.snapshot() == before && scene.redo() && scene.snapshot() == after,
          "Authored reverb did not retain one ordinary Scene Undo/Redo");
    const auto invalid_values =
        Json::array({Json::array({"reverb_decay", 0}), Json::array({"reverb_decay", 10.1}),
                     Json::array({"reverb_damping", -.01}), Json::array({"reverb_damping", .96}),
                     Json::array({"reverb_wet", -1}), Json::array({"reverb_wet", 1.1}),
                     Json::array({"reverb_enabled", "true"})});
    for (const auto& invalid : invalid_values) {
        reject([&] {
            authoring_command(scene, "property.set",
                              {{"entity", actor},
                               {"component", "forge.audio_source"},
                               {"field", invalid[0]},
                               {"value", invalid[1]}});
        });
        check(scene.snapshot() == after, "Rejected reverb edit changed authored intent");
    }
    Fixture invalid_native(root);
    configure(invalid_native, true, .5f);
    auto entity = invalid_native.scene.entity("speaker");
    auto value = entity.get<AudioSource>();
    value.reverb_decay = std::numeric_limits<float>::quiet_NaN();
    entity.set(value);
    invalid_native.simulation.sync_audio();
    check(invalid_native.audio->status().at("failed_sources") == 1,
          "Invalid native reverb configuration escaped admission");
    const auto checked = invalid_native.audio->status().at("configuration_checks");
    for (unsigned repeat = 0; repeat < 16; ++repeat)
        invalid_native.simulation.sync_audio();
    check(invalid_native.audio->status().at("configuration_checks") == checked,
          "Unchanged native NaN reverb configuration retried every synchronization");
    value.reverb_decay = .3f;
    entity.set(value);
    invalid_native.simulation.sync_audio();
    check(invalid_native.audio->wait_for_sources(std::chrono::seconds(5)) &&
              invalid_native.audio->status().at("failed_sources") == 0 &&
              invalid_native.energy(12000) > 1e-8,
          "Corrected native reverb configuration did not recover");
    std::cout
        << "Native source reverb: bypass, chain, memory, EOS/pause/resume/Stop/Restart passed\n";
}
