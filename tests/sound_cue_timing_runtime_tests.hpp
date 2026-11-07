#pragma once
#include "audio_source_access.hpp"
#include "audio_test_fixture.hpp"
#include <forge/sound_cue.hpp>

void sound_cue_timing_runtime_tests(const std::filesystem::path& root, AssetId clip) {
    const auto prepare = [&](Fixture& f, double delay, float pitch = 1, bool pause_with_game = true,
                             unsigned effects = 0) {
        auto doc = source(clip);
        auto& audio = doc["entities"][0]["components"]["forge.audio_source"];
        audio["play_on_start"] = false;
        audio["loop"] = false;
        audio["pitch"] = pitch;
        audio["pause_with_game"] = pause_with_game;
        audio["low_pass_enabled"] = bool(effects & 1);
        audio["high_pass_enabled"] = bool(effects & 2);
        audio["low_pass_hz"] = 1200;
        audio["high_pass_hz"] = 200;
        audio["echo_enabled"] = bool(effects & 4);
        audio["echo_seconds"] = .05;
        audio["reverb_enabled"] = bool(effects & 8);
        audio["reverb_decay"] = .5;
        doc["entities"].push_back({{"id", "cue"},
                                   {"name", "Delayed event"},
                                   {"components", {{"forge.sound_cue", sound_cue_json({})}}}});
        f.load(doc);
        SoundCue cue{true, 1, 0, {{0, f.ref(), {}, delay}}};
        f.scene.entity("cue").set(cue);
        f.audio->play_one_shot(delay == 0 ? f.ref() : f.ref("cue"));
        f.tick();
        f.audio->paused(false);
        if (std::llround(delay * 48000) > 0)
            check(AudioSourceAccess::status(*f.audio, f.ref()).at("delayed_one_shots") == 1,
                  "A reserved timed voice was absent from source observations");
    };
    const auto read = [](Fixture& f, unsigned frames) {
        std::vector<float> values(frames * 2);
        f.audio->read_offline(values);
        return values;
    };
    std::string context;
    const auto silence = [&](const auto& values) {
        if (!std::ranges::all_of(values, [](float x) { return x == 0; }))
            throw std::runtime_error("Delayed or held output was not silent: " + context);
    };
    const auto equal = [&](const auto& a, const auto& b, std::size_t offset = 0) {
        check(b.size() >= offset + a.size(), "Timing oracle buffer is short");
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!std::isfinite(b[offset + i]) || std::abs(a[i] - b[offset + i]) >= 1e-6f)
                throw std::runtime_error("Delayed output changed PCM: " + context + " sample=" +
                                         std::to_string(i) + " expected=" + std::to_string(a[i]) +
                                         " actual=" + std::to_string(b[offset + i]));
    };
    // Compare the actual delayed mix to an independent ordinary one-shot at each pitch.
    constexpr std::pair<double, unsigned> offsets[] = {
        {0, 0},          {.49 / 48000, 0},      {.5 / 48000, 1},
        {1. / 48000, 1}, {1776. / 48000, 1776}, {9600. / 48000, 9600},
        {10, 480000}};
    for (float pitch : {.25f, 1.f, 4.f})
        for (const auto& [seconds, delay] : offsets) {
            context = "pitch=" + std::to_string(pitch) + " delay=" + std::to_string(delay);
            Fixture immediate(root), delayed(root);
            prepare(immediate, 0, pitch);
            prepare(delayed, seconds, pitch);
            const auto a = read(immediate, 4096);
            const auto b = read(delayed, delay + 4096);
            silence(std::vector<float>(b.begin(), b.begin() + delay * 2));
            equal(a, b, delay * 2);
        }
    for (unsigned domain = 0; domain < 3; ++domain) {
        context = "pause-domain=" + std::to_string(domain);
        Fixture immediate(root), held(root);
        prepare(immediate, 0);
        prepare(held, 4096. / 48000);
        silence(read(held, 1024));
        if (domain == 0)
            held.audio->paused(true);
        else if (domain == 1)
            held.audio->game_paused(true);
        else
            AudioSourceAccess::control(*held.audio, held.ref(), AudioSourceOperation::Pause);
        silence(read(held, 8192));
        if (domain == 0)
            held.audio->paused(false);
        else if (domain == 1)
            held.audio->game_paused(false);
        else
            AudioSourceAccess::control(*held.audio, held.ref(), AudioSourceOperation::Resume);
        silence(read(held, 3072));
        equal(read(immediate, 2048), read(held, 2048));
    }
    {
        context = "independent game pause";
        Fixture immediate(root), independent(root);
        prepare(immediate, 0, 1, false);
        prepare(independent, 2048. / 48000, 1, false);
        independent.audio->game_paused(true);
        silence(read(independent, 2048));
        equal(read(immediate, 2048), read(independent, 2048));
    }
    {
        Fixture stopped(root);
        context = "stopped before onset";
        prepare(stopped, .1);
        AudioSourceAccess::control(*stopped.audio, stopped.ref(), AudioSourceOperation::Stop);
        silence(read(stopped, 16384));
        check(stopped.audio->status().at("one_shots") == 0,
              "Stop-before-onset retained a reserved delayed voice");
    }
    {
        Fixture immediate(root), rerouted(root);
        context = "live pause routing";
        prepare(immediate, 0, 1, false);
        prepare(rerouted, 4096. / 48000);
        silence(read(rerouted, 1024));
        rerouted.audio->game_paused(true);
        silence(read(rerouted, 8192));
        auto config = rerouted.scene.entity("speaker").get<AudioSource>();
        config.pause_with_game = false;
        rerouted.scene.entity("speaker").set(config);
        rerouted.audio->refresh_sources();
        silence(read(rerouted, 3072));
        equal(read(immediate, 2048), read(rerouted, 2048));
    }
    {
        Fixture replaced(root);
        context = "source component retired";
        prepare(replaced, .1);
        replaced.scene.entity("speaker").remove<AudioSource>();
        replaced.audio->refresh_sources();
        silence(read(replaced, 16384));
        check(replaced.audio->status().at("one_shots") == 0,
              "Retired native source retained an old delayed voice");
    }
    for (unsigned effects : {1u, 2u, 4u, 8u, 15u}) {
        context = "delayed effects=" + std::to_string(effects);
        Fixture immediate(root), delayed(root);
        prepare(immediate, 0, 1, true, effects);
        prepare(delayed, 1024. / 48000, 1, true, effects);
        silence(read(delayed, 512));
        AudioSourceAccess::control(*delayed.audio, delayed.ref(), AudioSourceOperation::Pause);
        silence(read(delayed, 4096));
        AudioSourceAccess::control(*delayed.audio, delayed.ref(), AudioSourceOperation::Resume);
        silence(read(delayed, 512));
        equal(read(immediate, 65000), read(delayed, 65000));
    }
    {
        Fixture bounded(root);
        context = "voice reservation and stop";
        prepare(bounded, 1);
        for (unsigned i = 0; i < 16; ++i)
            bounded.audio->play_one_shot(bounded.ref("cue"));
        bounded.audio->refresh_sources();
        check(AudioSourceAccess::status(*bounded.audio, bounded.ref()).at("delayed_one_shots") ==
                      16 &&
                  bounded.audio->status().at("one_shots") == 16,
              "Delayed voices bypassed the existing per-source reservation limit");
        AudioSourceAccess::control(*bounded.audio, bounded.ref(), AudioSourceOperation::Stop);
        silence(read(bounded, 65536));
    }
}
