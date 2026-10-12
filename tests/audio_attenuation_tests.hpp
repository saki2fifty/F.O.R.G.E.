#pragma once
#include <algorithm>
#include <array>
#include <limits>
// Actual offline PCM, independent distance equations and existing voice ownership.
void attenuation_tests(const std::filesystem::path& root, AssetId clip) {
    Fixture f(root);
    f.load(source(clip)); // Old documents omit the new fields.
    auto actor = f.scene.entity("speaker");
    auto config = actor.get<AudioSource>();
    check(config.attenuation == 0 && config.rolloff == 1,
          "Legacy sound did not retain inverse distance at strength one");
    const auto listener = authoring_command(f.scene, "entity.create", {{"name", "Listener"}})
                              .at("selected")
                              .get<std::string>();
    authoring_command(f.scene, "component.add",
                      {{"entity", listener}, {"component", "forge.audio_listener"}});
    f.scene.entity(listener).set<LocalTranslation>({0, 0, 0});
    config.spatialized = true;
    config.minimum_distance = 2;
    config.maximum_distance = 10;
    f.simulation.audio_paused(false);
    auto energy_at = [&](double distance) {
        actor.set(config);
        actor.set<LocalTranslation>({0, 0, distance});
        f.tick();
        (void)f.energy(4096); // Complete the backend's finite gain ramp using PCM frames.
        check(f.audio->status().at("voices") == 1 && f.audio->status().at("playing_sources") == 1,
              "Changing falloff replaced or stopped the active voice");
        return f.energy(4800); // Exactly44 sine periods; phase-independent power.
    };
    const auto nominal = energy_at(2);
    check(nominal > 1e-5, "Spatial falloff reference is silent");
    for (config.attenuation = 0; config.attenuation < 3; ++config.attenuation)
        for (float strength : {0.f, .5f, 1.f, 2.f, 10.f}) {
            config.rolloff = strength;
            for (double distance : {.25, 2., 3., 6., 10., 20.}) {
                const double d = std::clamp(distance, 2., 10.);
                double amplitude = config.attenuation == 0   ? 2 / (2 + strength * (d - 2))
                                   : config.attenuation == 1 ? 1 - strength * (d - 2) / 8
                                                             : std::pow(d / 2, -strength);
                amplitude = std::clamp(amplitude, 0., 1.);
                const auto actual = energy_at(distance) / nominal;
                if (std::abs(actual - amplitude * amplitude) >= .004)
                    std::cerr << "Falloff mode " << config.attenuation << ", strength " << strength
                              << ", distance " << distance << ": power ratio " << actual
                              << ", expected " << amplitude * amplitude << '\n';
                check(std::abs(actual - amplitude * amplitude) < .004,
                      "Offline PCM differs from independent clamped distance equation");
            }
            config.minimum_distance = config.maximum_distance = 2;
            check(std::abs(energy_at(20) / nominal - 1) < .004,
                  "Equal sound distance bounds did not retain legacy constant gain");
            config.maximum_distance = 10;
        }
    config.attenuation = 2;
    config.rolloff = 10;
    config.spatialized = false;
    const auto nonspatial = energy_at(2);
    check(std::abs(energy_at(200) / nonspatial - 1) < .004,
          "Distance falloff affected a nonspatial source");

    config.spatialized = true;
    config.rolloff = 0;
    auto channels_at = [&](double x) {
        actor.set(config);
        actor.set<LocalTranslation>({x, 0, 0});
        f.tick();
        (void)f.energy(4096);
        std::vector<float> pcm(9600);
        f.audio->read_offline(pcm);
        std::array<double, 2> power{};
        for (std::size_t i = 0; i < pcm.size(); ++i)
            power[i % 2] += double(pcm[i]) * pcm[i];
        return power;
    };
    const auto left = channels_at(-5), right = channels_at(5);
    check((left[0] - left[1]) * (right[0] - right[1]) < 0 &&
              std::max(left[0], left[1]) > 2 * std::min(left[0], left[1]) &&
              std::abs((left[0] + left[1]) / (right[0] + right[1]) - 1) < .004,
          "Zero falloff strength lost positional stereo panning");

    const auto authored = f.scene.snapshot();
    for (const Json value : {Json(-1), Json(3), Json(.5), Json("Linear")}) {
        reject([&] {
            authoring_command(f.scene, "property.set",
                              {{"entity", "speaker"},
                               {"component", "forge.audio_source"},
                               {"field", "attenuation"},
                               {"value", value}});
        });
        check(f.scene.snapshot() == authored, "Invalid falloff edit changed authored sound");
    }
    for (const Json value : {Json(-.1), Json(10.1), Json("1")}) {
        reject([&] {
            authoring_command(f.scene, "property.set",
                              {{"entity", "speaker"},
                               {"component", "forge.audio_source"},
                               {"field", "rolloff"},
                               {"value", value}});
        });
        check(f.scene.snapshot() == authored, "Invalid strength edit changed authored sound");
    }
    config.attenuation = 3;
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 0, "Invalid native falloff entered backend mapping");
    config.attenuation = 0;
    config.rolloff = std::numeric_limits<float>::quiet_NaN();
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 0, "Nonfinite native falloff entered mixer");
    config.rolloff = 1;
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 1, "Corrected sound did not recover admission");

    Fixture uninterrupted(root), changing(root);
    uninterrupted.load(source(clip));
    changing.load(source(clip));
    uninterrupted.simulation.audio_paused(false);
    changing.simulation.audio_paused(false);
    std::vector<float> a(8192), b(8192);
    uninterrupted.audio->read_offline(a);
    changing.audio->read_offline(b);
    check(a == b, "Voice continuity fixtures differ initially");
    a.resize(200);
    b.resize(200);
    for (unsigned tick = 0; tick < 10; ++tick) {
        auto c = changing.scene.entity("speaker").get<AudioSource>();
        c.attenuation = tick % 3;
        c.rolloff = float(tick);
        changing.scene.entity("speaker").set(c);
        uninterrupted.tick();
        changing.tick();
        uninterrupted.audio->read_offline(a);
        changing.audio->read_offline(b);
        check(a == b, "Updating falloff restarted the existing sample cursor");
    }
}
