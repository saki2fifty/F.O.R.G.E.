#pragma once
#include <array>
#include <limits>
void custom_mix_tests(const std::filesystem::path& root, AssetId clip) {
    const AudioRouting graph{{{"room", "Room", .75f, "effects", {{"dialogue", .5f}}},
                              {"rain", "Rain", .5f, "room", {{"music", .25f}}}}};
    Fixture reference(root), routed(root, 1, false, graph);
    reference.load(source(clip));
    auto doc = source(clip);
    doc["entities"][0]["components"]["forge.audio_source"]["custom_bus"] = "rain";
    routed.load(doc);
    reference.simulation.audio_paused(false);
    routed.simulation.audio_paused(false);
    (void)reference.energy(4096);
    (void)routed.energy(4096);
    const double nominal = reference.energy(4800);
    const std::array<float, 3> channels{.25f, .5f, .75f};
    routed.audio->mix_volumes(.5f, channels);
    (void)routed.energy(4096);
    // Rain .5 -> Room .75 -> Effects + Dialogue*.5; Rain also sends Music*.25.
    const double amplitude = .5 * (.5 * .75 * (.5 + .5 * .75) + .5 * .25 * .25);
    check(std::abs(routed.energy(4800) / nominal - amplitude * amplitude) < .001,
          "Custom sends did not sum through terminal and Master faders in actual PCM");
    check(routed.audio->status().at("bus_volumes") == Json({{"rain", .5}, {"room", .75}}),
          "Legacy mix command erased custom gains");
    const auto before = routed.audio->status();
    for (const AudioBusVolumes& invalid :
         {AudioBusVolumes{{"rain", 1}}, AudioBusVolumes{{"rain", 1}, {"room", 1}, {"other", 1}},
          AudioBusVolumes{{"rain", 1}, {"room", -1}}}) {
        reject([&] { routed.audio->mix_volumes(.125f, {0, 0, 0}, invalid); });
        check(routed.audio->status() == before,
              "Rejected custom mix partially changed live output/status");
    }
    routed.audio->mix_volumes(1, {1, 1, 1}, {{"rain", 0}, {"room", 1}});
    (void)routed.energy(4096);
    check(routed.energy(4800) < 1e-12 && routed.audio->status().at("playing_sources") == 1,
          "Muted custom bus stopped playback or leaked output");
    routed.audio->mix_volumes(1, {1, 1, 1}, {{"rain", 1}, {"room", 1}});
    (void)routed.energy(4096);
    check(std::abs(routed.energy(4800) / nominal - 1.75 * 1.75) < .004,
          "Custom unmute did not restore the authored additive sends");
    auto config = routed.scene.entity("speaker").get<AudioSource>();
    config.custom_bus = "missing";
    routed.scene.entity("speaker").set(config);
    routed.tick();
    check(routed.audio->status().at("voices") == 0 &&
              routed.audio->status().at("failed_sources") == 1,
          "Unresolved custom route silently used the stock channel");
    config.custom_bus = "room";
    routed.scene.entity("speaker").set(config);
    routed.tick();
    check(routed.audio->status().at("voices") == 1 &&
              routed.audio->status().at("failed_sources") == 0,
          "Corrected custom route remained cached as a failed source");

    const AudioRouting unit_routes{
        {{"first", "", 1, "effects", {}}, {"second", "", 1, "dialogue", {}}}};
    Fixture uninterrupted(root), changed(root, 1, false, unit_routes);
    for (auto* world : {&uninterrupted, &changed}) {
        world->load(source(clip));
        world->simulation.audio_paused(false);
    }
    for (unsigned i = 0; i < 12; ++i) {
        auto value = changed.scene.entity("speaker").get<AudioSource>();
        value.custom_bus = i % 3 == 0 ? "" : i % 3 == 1 ? "first" : "second";
        changed.scene.entity("speaker").set(value);
        uninterrupted.tick();
        changed.tick();
        std::array<float, 512> expected{}, actual{};
        uninterrupted.audio->read_offline(expected);
        changed.audio->read_offline(actual);
        check(expected == actual, "Custom reroute reset or shifted the source sample cursor");
    }
    changed.audio->mix_volumes(1, {1, 1, 1}, {{"first", 0}, {"second", 0}});
    std::array<float, 4096> ignored{};
    uninterrupted.audio->read_offline(ignored);
    changed.audio->read_offline(ignored);
    changed.audio->mix_volumes(1, {1, 1, 1}, {{"first", 1}, {"second", 1}});
    for (unsigned i = 0; i < 3; ++i) {
        std::array<float, 1024> expected{}, actual{};
        uninterrupted.audio->read_offline(expected);
        changed.audio->read_offline(actual);
        if (i == 2)
            check(expected == actual, "Custom fader mute/unmute shifted the sample cursor");
    }
    const AudioRouting effect_routes{{{"wet", "", 1, "music", {{"effects", .5f}}}}};
    Fixture effect_reference(root), effect_routed(root, 1, false, effect_routes);
    auto effect_doc = source(clip);
    auto& effect = effect_doc["entities"][0]["components"]["forge.audio_source"];
    effect["echo_enabled"] = true;
    effect["echo_seconds"] = .02f;
    effect["echo_feedback"] = .5f;
    effect["reverb_enabled"] = true;
    effect["reverb_wet"] = .5f;
    effect_reference.load(effect_doc);
    effect["custom_bus"] = "wet";
    effect_routed.load(effect_doc);
    for (auto* world : {&effect_reference, &effect_routed}) {
        world->simulation.audio_paused(false);
        world->audio->play_one_shot(world->ref());
        world->tick();
    }
    for (unsigned i = 0; i < 8; ++i) {
        std::array<float, 4096> expected{}, actual{};
        effect_reference.audio->read_offline(expected);
        effect_routed.audio->read_offline(actual);
        for (std::size_t j = 0; j < actual.size(); ++j)
            check(std::abs(actual[j] - expected[j] * 1.5f) < 1e-6f,
                  "Custom sends changed effect/one-shot PCM instead of scaling its output");
    }
    check(effect_routed.audio->status().at("one_shots") == 1 &&
              effect_routed.audio->status().at("reverb_buffer_bytes") > 0 &&
              effect_routed.audio->status().at("echo_buffer_bytes") > 0,
          "Custom routing fixture did not exercise both effect-bearing voices");
    effect_routed.audio->mix_volumes(1, {1, 1, 1}, {{"wet", 0}});
    (void)effect_routed.energy(4096);
    check(effect_routed.energy(4800) < 1e-12,
          "Custom bus mute leaked buffered effects or one-shot output");
}
void mix_tests(const std::filesystem::path& root, AssetId clip) {
    custom_mix_tests(root, clip);
    Fixture f(root);
    f.load(source(clip));
    f.simulation.audio_paused(false);
    auto actor = f.scene.entity("speaker");
    auto config = actor.get<AudioSource>();
    check(config.bus == 0, "Legacy source did not route to Master");
    (void)f.energy(4096);
    const double nominal = f.energy(4800);
    check(nominal > 1e-5, "Mix reference is silent");
    const std::array channels{.25f, .5f, .75f};
    f.audio->mix_volumes(.4f, channels);
    for (unsigned bus = 0; bus < 4; ++bus) {
        config.bus = bus;
        actor.set(config);
        f.tick();
        (void)f.energy(4096);
        const double amplitude = .4 * (bus == 0 ? 1 : channels[bus - 1]);
        check(std::abs(f.energy(4800) / nominal - amplitude * amplitude) < .004,
              "Actual routed PCM did not multiply channel and Master volume");
    }
    const auto before = f.audio->status();
    for (float invalid : {-1.f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
        reject([&] { f.audio->mix_volumes(.9f, {.3f, .4f, invalid}); });
        check(f.audio->status().at("master_volume") == before.at("master_volume") &&
                  f.audio->status().at("channel_volumes") == before.at("channel_volumes"),
              "Invalid later mix channel partially changed earlier volumes");
        reject([&] { f.audio->mix_volumes(invalid, channels); });
    }
    f.audio->master_volume(.6f);
    check(f.audio->status().at("channel_volumes") == channels,
          "Master-only update erased channel settings");
    f.audio->mix_volumes(1, {1, 1, 0});
    (void)f.energy(4096);
    check(f.energy(4800) < 1e-12 && f.audio->status().at("playing_sources") == 1,
          "Muted Dialogue channel stopped voice or produced audible output");
    f.audio->mix_volumes(1, {1, 1, 1});
    (void)f.energy(4096);
    check(std::abs(f.energy(4800) / nominal - 1) < .004, "Dialogue unmute did not restore output");
    const auto authored = f.scene.snapshot();
    reject([&] {
        authoring_command(f.scene, "property.set",
                          {{"entity", "speaker"},
                           {"component", "forge.audio_source"},
                           {"field", "bus"},
                           {"value", 4}});
    });
    check(f.scene.snapshot() == authored, "Invalid authored channel changed scene");
    config.bus = 4;
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 0, "Invalid native route reached channel array");
    config.bus = 2;
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 1, "Corrected native route did not recover");

    Fixture continuous(root), rerouted(root);
    for (auto* world : {&continuous, &rerouted}) {
        world->load(source(clip));
        world->simulation.audio_paused(false);
    }
    for (unsigned i = 0; i < 12; ++i) {
        auto value = rerouted.scene.entity("speaker").get<AudioSource>();
        value.bus = i % 4;
        rerouted.scene.entity("speaker").set(value);
        continuous.tick();
        rerouted.tick();
        std::array<float, 512> expected{}, actual{};
        continuous.audio->read_offline(expected);
        rerouted.audio->read_offline(actual);
        if (actual != expected) {
            double maximum = 0;
            for (unsigned k = 0; k < actual.size(); ++k)
                maximum = std::max(maximum, std::abs(double(actual[k]) - expected[k]));
            std::cerr << "Route step " << i << " max PCM difference " << maximum
                      << "; first samples " << expected[0] << '/' << actual[0] << '\n';
        }
        check(actual == expected, "Live bus reroute reset or shifted playing sample cursor");
    }
    // A muted group must continue consuming source samples, unlike gameplay pause.
    continuous.audio->mix_volumes(1, {1, 1, 1});
    rerouted.audio->mix_volumes(1, {1, 1, 0});
    std::array<float, 4096> ignored{};
    continuous.audio->read_offline(ignored);
    rerouted.audio->read_offline(ignored);
    rerouted.audio->mix_volumes(1, {1, 1, 1});
    for (unsigned i = 0; i < 3; ++i) {
        std::array<float, 1024> expected{}, actual{};
        continuous.audio->read_offline(expected);
        rerouted.audio->read_offline(actual);
        if (i == 2)
            check(expected == actual, "Unmute did not preserve source sample cursor");
    }
    Fixture sum(root);
    auto doc = source(clip);
    doc["entities"][0]["components"]["forge.audio_source"]["bus"] = 1;
    auto other = doc["entities"][0];
    other["id"] = "other";
    other["components"]["forge.audio_source"]["bus"] = 2;
    doc["entities"].push_back(other);
    sum.load(doc);
    sum.simulation.audio_paused(false);
    sum.audio->mix_volumes(.5f, {.25f, .5f, 1});
    (void)sum.energy(4096);
    const double amplitude = .5 * (.25 + .5);
    check(std::abs(sum.energy(4800) / nominal - amplitude * amplitude) < .004,
          "Independent channel voices did not sum through Master");
    Fixture unresolved(root);
    auto spatial = source(clip);
    spatial["entities"][0]["components"]["forge.audio_source"]["spatialized"] = true;
    spatial["entities"][0]["components"]["forge.audio_source"]["bus"] = 1;
    unresolved.load(spatial);
    unresolved.simulation.audio_paused(false);
    unresolved.audio->mix_volumes(1, {1, 1, 1});
    check(unresolved.energy(4800) < 1e-12,
          "Live mixer update unmuted a spatial source with no valid listener");
}
