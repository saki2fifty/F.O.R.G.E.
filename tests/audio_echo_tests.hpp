#pragma once
void audio_echo_tests(const std::filesystem::path& root, AssetId tone) {
    auto pcm = [](Fixture& f, unsigned frames) {
        std::vector<float> data(frames * 2);
        f.audio->read_offline(data);
        for (float sample : data)
            check(std::isfinite(sample), "Echo produced nonfinite PCM");
        return data;
    };
    auto load = [&](Fixture& f, Json doc, bool echo, float seconds, float feedback,
                    bool filtered = false) {
        auto& config = doc["entities"][0]["components"]["forge.audio_source"];
        config["echo_enabled"] = echo;
        config["echo_seconds"] = seconds;
        config["echo_feedback"] = feedback;
        config["low_pass_enabled"] = filtered;
        config["low_pass_hz"] = 1200.f;
        f.load(doc);
        f.simulation.audio_paused(false);
    };
    for (bool filtered : {false, true}) {
        Fixture dry(root), echo(root), dormant(root), zero(root);
        const auto doc = source(tone);
        load(dry, doc, false, .25f, .35f, filtered);
        load(echo, doc, true, .03125f, .5f, filtered);
        load(dormant, doc, false, 1, .75f, filtered);
        load(zero, doc, true, .01f, 0, filtered);
        auto reference = pcm(dry, 16000);
        const auto actual = pcm(echo, 16000);
        check(*std::max_element(reference.begin(), reference.end()) > .01f,
              "Independent echo oracle has a silent dry reference");
        check(pcm(dormant, 16000) == reference && pcm(zero, 16000) == reference,
              "Disabled/zero-feedback echo changed unprocessed or filtered PCM");
        check(dry.audio->status().at("echo_buffer_bytes") == 0 &&
                  dormant.audio->status().at("echo_buffer_bytes") == 0 &&
                  zero.audio->status().at("echo_buffer_bytes") == 0 &&
                  echo.audio->status().at("echo_buffer_bytes") == 12000,
              "Echo bypass allocated a delay buffer or active storage exceeded its exact bound");
        // Independent scalar recurrence over the dry native output, not the
        // native delay implementation or a copied native processing loop.
        for (std::size_t i = 3000; i < reference.size(); ++i)
            reference[i] += .5f * reference[i - 3000];
        double maximum_error = 0;
        for (std::size_t i = 0; i < reference.size(); ++i)
            maximum_error = std::max(maximum_error, std::abs(double(actual[i] - reference[i])));
        check(maximum_error < .000003, "Echo timing/feedback differs from independent PCM oracle");
    }
    // Original 50ms impulse clip: its first100ms echo occurs after clip EOS.
    const auto impulse_path = root / "Assets/echo-impulse.wav";
    {
        std::ofstream out(impulse_path, std::ios::binary);
        auto integer = [&](std::uint32_t value, unsigned bytes) {
            for (unsigned i = 0; i < bytes; ++i)
                out.put(char(value >> (8 * i)));
        };
        out.write("RIFF", 4);
        integer(36 + 2400 * 2, 4);
        out.write("WAVEfmt ", 8);
        integer(16, 4);
        integer(1, 2);
        integer(1, 2);
        integer(48000, 4);
        integer(96000, 4);
        integer(2, 2);
        integer(16, 2);
        out.write("data", 4);
        integer(2400 * 2, 4);
        for (unsigned i = 0; i < 2400; ++i)
            integer(i == 256 ? 8192 : 0, 2);
    }
    const auto impulse = AssetCatalog::register_audio_clip(root, "Assets/echo-impulse.wav").id;
    auto doc = source(impulse);
    auto& config = doc["entities"][0]["components"]["forge.audio_source"];
    config["loop"] = false;
    config["play_on_start"] = false;
    Fixture shots(root);
    load(shots, doc, true, .1f, .5f);
    shots.audio->play_one_shot(shots.ref());
    shots.audio->refresh_sources();
    const auto first = pcm(shots, 3200);
    // The native resampler has a small startup lead; measure its impulse peak
    // rather than claiming the input sample index is an output-clock position.
    check(std::abs(*std::max_element(first.begin(), first.end()) - .25f) < .00001f &&
              shots.audio->status().at("one_shots") == 1,
          "Echo one-shot was silent or retired at clip EOS");
    shots.simulation.audio_paused(true);
    check(shots.energy(100000) == 0 && shots.audio->status().at("one_shots") == 1,
          "Game pause advanced or leaked a post-EOS echo tail");
    shots.simulation.audio_paused(false);
    shots.simulation.suspend_audio();
    check(shots.energy(100000) == 0 && shots.audio->status().at("one_shots") == 1,
          "Full suspension advanced or leaked a post-EOS echo tail");
    shots.simulation.activate_audio(false);
    shots.audio->pause_source(shots.ref());
    shots.audio->refresh_sources();
    check(shots.energy(100000) == 0 && shots.audio->status().at("one_shots") == 1,
          "Paused post-EOS echo leaked sound or expired on output time");
    shots.audio->resume_source(shots.ref());
    shots.audio->refresh_sources();
    const auto resumed = pcm(shots, 6400);
    check(std::abs(*std::max_element(resumed.begin(), resumed.end()) - .125f) < .00001f,
          "Resume lost the post-EOS one-shot echo or changed its first repeat gain");
    for (unsigned i = 0; i < 12; ++i)
        (void)pcm(shots, 8192);
    check(shots.audio->status().at("one_shots") == 0,
          "Completed echo one-shot retained its voice beyond the bounded tail");
    config["play_on_start"] = true;
    Fixture primary(root);
    load(primary, doc, true, .1f, .5f);
    (void)pcm(primary, 3200);
    primary.audio->pause_source(primary.ref());
    primary.audio->refresh_sources();
    check(primary.energy(100000) == 0, "Primary post-EOS echo ignored source Pause");
    primary.audio->resume_source(primary.ref());
    primary.audio->refresh_sources();
    check(primary.energy(6400) > .0000001, "Primary post-EOS echo could not Resume");
    primary.audio->stop_source(primary.ref());
    primary.audio->refresh_sources();
    check(primary.energy(12000) == 0, "Source Stop leaked retained echo");
    primary.audio->play(primary.ref());
    primary.audio->refresh_sources();
    const auto restarted = pcm(primary, 3200);
    check(std::abs(*std::max_element(restarted.begin(), restarted.end()) - .25f) < .00001f,
          "Restart lost the impulse or retained the old echo history");
    for (auto [field, bad] : {std::pair{"echo_seconds", .009f},
                              {"echo_seconds", 1.01f},
                              {"echo_feedback", -.01f},
                              {"echo_feedback", .76f}}) {
        auto invalid = doc;
        invalid["entities"][0]["components"]["forge.audio_source"][field] = bad;
        const auto before = primary.scene.snapshot();
        reject([&] { primary.scene.restore_snapshot(invalid); });
        check(primary.scene.snapshot() == before, "Rejected echo settings mutated the Scene");
    }
    config["pause_with_game"] = false;
    Fixture independent(root);
    load(independent, doc, true, .1f, .5f);
    (void)pcm(independent, 3200);
    independent.simulation.audio_paused(true);
    const auto independent_tail = pcm(independent, 6400);
    check(std::abs(*std::max_element(independent_tail.begin(), independent_tail.end()) - .125f) <
              .00001f,
          "Opted-out echo tail stopped with gameplay or restarted its clip");
    independent.simulation.suspend_audio();
    check(independent.energy(100000) == 0, "Full suspension leaked an opted-out echo");
    independent.simulation.activate_audio(true);
    const auto next_repeat = pcm(independent, 6400);
    check(std::abs(*std::max_element(next_repeat.begin(), next_repeat.end()) - .0625f) < .00001f &&
              independent.audio->status().at("tick") == 0,
          "Full Resume lost independent echo history or advanced the fixed clock");
}
