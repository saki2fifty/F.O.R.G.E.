#pragma once
#include <array>
#include <limits>
void mix_tests(const std::filesystem::path& root, AssetId clip) {
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
