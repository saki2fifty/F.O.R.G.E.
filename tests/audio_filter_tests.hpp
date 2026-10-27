#pragma once
#include "../src/audio_preview_access.hpp"
#include <limits>

void audio_filter_tests(const std::filesystem::path& root, AssetId clip) {
    Fixture plain(root), low(root), high(root), band(root);
    const auto doc = source(clip);
    plain.load(doc);
    auto filtered = [&](Fixture& f, bool lp, float low_hz, bool hp, float high_hz) {
        auto candidate = doc;
        auto& value = candidate["entities"][0]["components"]["forge.audio_source"];
        value["low_pass_enabled"] = lp;
        value["low_pass_hz"] = low_hz;
        value["high_pass_enabled"] = hp;
        value["high_pass_hz"] = high_hz;
        f.load(candidate);
    };
    filtered(low, true, 100, false, 20);
    filtered(high, false, 20000, true, 1000);
    filtered(band, true, 2000, true, 1000);
    Fixture bypass(root);
    filtered(bypass, false, 20, false, 20000);
    plain.simulation.activate_audio(false);
    bypass.simulation.activate_audio(false);
    std::vector<float> expected_pcm(8192), bypass_pcm(8192);
    plain.audio->read_offline(expected_pcm);
    bypass.audio->read_offline(bypass_pcm);
    check(expected_pcm == bypass_pcm,
          "Disabled filter cutoffs changed the original unprocessed PCM");
    for (Fixture* f : {&plain, &low, &high, &band}) {
        f->simulation.activate_audio(false);
        (void)f->energy(8192); // Settle filter state and native graph caches.
    }
    const auto reference = plain.energy(4800);
    const auto low_ratio = low.energy(4800) / reference;
    const auto high_ratio = high.energy(4800) / reference;
    const auto band_ratio = band.energy(4800) / reference;
    // Independent second-order Butterworth power response, with bilinear warping.
    auto response = [](double cutoff, bool high_pass) {
        constexpr double pi = 3.141592653589793;
        const double ratio = std::tan(pi * 440 / 48000) / std::tan(pi * cutoff / 48000);
        return 1 / (1 + std::pow(high_pass ? 1 / ratio : ratio, 4));
    };
    check(std::abs(low_ratio / response(100, false) - 1) < .02,
          "Low-pass PCM differs from independent frequency response");
    check(std::abs(high_ratio / response(1000, true) - 1) < .02,
          "High-pass PCM differs from independent frequency response");
    check(std::abs(band_ratio / (response(2000, false) * response(1000, true)) - 1) < .02,
          "Combined filter PCM differs from independent frequency response");

    auto actor = low.scene.entity("speaker");
    auto config = actor.get<AudioSource>();
    const auto cursor = AudioPreviewAccess::elapsed(*low.audio, low.ref());
    config.low_pass_enabled = false;
    actor.set(config);
    low.audio->refresh_sources();
    check(AudioPreviewAccess::elapsed(*low.audio, low.ref()) == cursor,
          "Filter bypass restarted the source cursor");
    (void)low.energy(8192);
    check(std::abs(low.energy(4800) / reference - 1) < .002,
          "Disabled filters did not restore the unprocessed source");
    config.low_pass_enabled = true;
    config.low_pass_hz = 100;
    config.pause_with_game = false;
    actor.set(config);
    low.audio->refresh_sources();
    low.simulation.audio_paused(true);
    (void)low.energy(8192);
    check(std::abs(low.energy(4800) / reference / low_ratio - 1) < .02,
          "Filter routing lost the independent pause policy");
    config.pause_with_game = true;
    actor.set(config);
    low.audio->refresh_sources();
    (void)low.energy(8192);
    check(low.energy(4800) < 1e-12, "Filtered source escaped ordinary gameplay pause");

    // One-shots own independent native filters and retain their cursor on updates.
    Fixture effects(root);
    auto shots = doc;
    auto& settings = shots["entities"][0]["components"]["forge.audio_source"];
    settings["play_on_start"] = false;
    settings["low_pass_enabled"] = true;
    settings["low_pass_hz"] = 100.f;
    effects.load(shots);
    effects.simulation.activate_audio(false);
    effects.audio->play_one_shot(effects.ref());
    effects.audio->refresh_sources();
    (void)effects.energy(8192);
    check(std::abs(effects.energy(4800) / reference / low_ratio - 1) < .02,
          "One-shot did not inherit the source filter");
    auto next = effects.scene.entity("speaker").get<AudioSource>();
    next.low_pass_enabled = false;
    effects.scene.entity("speaker").set(next);
    effects.audio->refresh_sources();
    (void)effects.energy(8192);
    check(effects.audio->status().at("one_shots") == 1 &&
              std::abs(effects.energy(4800) / reference - 1) < .002,
          "Live one-shot bypass restarted, lost or failed to update the effect");
    effects.simulation.suspend_audio();
    check(effects.energy(4800) == 0, "Full suspension leaked a filter output");

    Fixture authored(root);
    authored.load(doc);
    const auto authored_id = authored.ref().entity.str();
    authoring_command(authored.scene, "property.set_many",
                      {{"entity", authored_id},
                       {"component", "forge.audio_source"},
                       {"values",
                        {{"low_pass_enabled", true},
                         {"low_pass_hz", 20.f},
                         {"high_pass_enabled", true},
                         {"high_pass_hz", 20000.f}}}});
    check(authored.scene.entity("speaker").get<AudioSource>().low_pass_hz == 20 &&
              authored.scene.entity("speaker").get<AudioSource>().high_pass_hz == 20000,
          "Valid boundary filter values were not authored");
    const auto before = authored.scene.snapshot();
    for (const auto* field : {"low_pass_hz", "high_pass_hz"})
        for (Json bad : {Json(0), Json(20001), Json("100")}) {
            reject([&] {
                authoring_command(authored.scene, "property.set",
                                  {{"entity", authored_id},
                                   {"component", "forge.audio_source"},
                                   {"field", field},
                                   {"value", bad}});
            });
            check(authored.scene.snapshot() == before,
                  "Invalid filter setting changed the admitted scene");
        }
    auto invalid = authored.scene.entity("speaker").get<AudioSource>();
    invalid.low_pass_hz = std::numeric_limits<float>::quiet_NaN();
    authored.scene.entity("speaker").set(invalid);
    authored.audio->refresh_sources();
    check(authored.audio->status().at("failed_sources") == 1,
          "Native nonfinite filter value escaped reflected admission");
    const auto checks = authored.audio->status().at("configuration_checks");
    authored.audio->refresh_sources();
    check(authored.audio->status().at("configuration_checks") == checks,
          "Repeated invalid filter configuration was not suppressed");
    invalid.low_pass_hz = 1000;
    authored.scene.entity("speaker").set(invalid);
    authored.audio->refresh_sources();
    check(authored.audio->status().at("failed_sources") == 0,
          "Repaired native filter configuration failed to recover");
}
