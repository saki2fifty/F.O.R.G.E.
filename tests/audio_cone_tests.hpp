#pragma once
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
void cone_tests(const std::filesystem::path& root, AssetId clip) {
    auto listener_at_origin = [](Fixture& f) {
        const std::string id = authoring_command(f.scene, "entity.create").at("selected");
        authoring_command(f.scene, "component.add",
                          {{"entity", id}, {"component", "forge.audio_listener"}});
        f.scene.entity(id).set<LocalTranslation>({0, 0, 0});
    };
    auto rotation = [](double degrees) {
        const double half = degrees * std::numbers::pi / 360;
        return LocalRotation{0, float(std::sin(half)), 0, float(std::cos(half))};
    };
    Fixture f(root);
    f.load(source(clip));
    listener_at_origin(f);
    auto actor = f.scene.entity("speaker");
    auto config = actor.get<AudioSource>();
    check(!config.cone_enabled && config.inner_angle == 60 && config.outer_angle == 120 &&
              config.outer_gain == .25f,
          "Legacy source did not retain omnidirectional defaults");
    config.spatialized = true;
    config.rolloff = 0;
    actor.set<LocalTranslation>({0, 0, 6});
    f.simulation.audio_paused(false);
    auto energy_at = [&](double degrees) {
        actor.set(config);
        actor.set(rotation(degrees));
        f.tick();
        (void)f.energy(4096);
        check(f.audio->status().at("voices") == 1 && f.audio->status().at("playing_sources") == 1,
              "Angular update replaced or stopped its voice");
        return f.energy(4800);
    };
    const double nominal = energy_at(0);
    check(nominal > 1e-5, "Angular reference is silent");
    config.cone_enabled = true;
    struct Cone {
        float inner, outer, outside;
    };
    for (const auto cone : std::array{Cone{60, 120, .25f}, Cone{0, 90, 0}, Cone{90, 180, 1},
                                      Cone{360, 360, 0}, Cone{120, 120, .25f}, Cone{0, 0, .25f}}) {
        config.inner_angle = cone.inner;
        config.outer_angle = cone.outer;
        config.outer_gain = cone.outside;
        for (double degrees : {0., 10., 25., 45., 70., 90., 120., 179.}) {
            double amplitude = 1;
            if (cone.inner < 360) {
                const auto dot = std::cos(degrees * std::numbers::pi / 180);
                const auto inside = std::cos(cone.inner * std::numbers::pi / 360);
                const auto outside = std::cos(cone.outer * std::numbers::pi / 360);
                if (!(dot > inside))
                    amplitude = dot > outside
                                    ? cone.outside +
                                          (1 - cone.outside) * (dot - outside) / (inside - outside)
                                    : cone.outside;
            }
            const double actual = energy_at(degrees) / nominal;
            if (std::abs(actual - amplitude * amplitude) >= .004)
                std::cerr << "Cone " << cone.inner << '/' << cone.outer << '/' << cone.outside
                          << " at " << degrees << " deg: power ratio " << actual << ", expected "
                          << amplitude * amplitude << '\n';
            check(std::abs(actual - amplitude * amplitude) < .004,
                  "Actual angular PCM did not match independent cosine interpolation");
        }
    }
    config.inner_angle = 60;
    config.outer_angle = 120;
    config.outer_gain = .25f;
    config.minimum_distance = 2;
    config.maximum_distance = 10;
    config.attenuation = 0;
    config.rolloff = 2;
    const double angular =
        .25 + .75 * (std::cos(std::numbers::pi / 4) - .5) / (std::cos(std::numbers::pi / 6) - .5);
    check(std::abs(energy_at(45) / nominal - std::pow(angular * .2, 2)) < .004,
          "Angular gain and distance falloff did not multiply");
    config.rolloff = 0;
    actor.set<LocalTranslation>({0, 0, 0});
    config.cone_enabled = false;
    const auto coincident = energy_at(0);
    config.cone_enabled = true;
    check(std::abs(energy_at(179) / coincident - 1) < .004,
          "Coincident source/listener should bypass undefined angular loss");
    const std::string parent = authoring_command(f.scene, "entity.create").at("selected");
    f.scene.entity(parent).set<LocalTranslation>({0, 0, 6});
    f.scene.entity(parent).set(rotation(180));
    f.scene.entity(parent).set<LocalScale>({3, 2, 4});
    actor.set<LocalTranslation>({0, 0, 0});
    f.scene.reparent_entity("speaker", parent, ReparentMode::KeepLocal);
    check(std::abs(energy_at(0) / nominal - .0625) < .004,
          "Effective parent rotation/normalized nonuniform scale did not aim emitter");
    actor.set<SpatialBinding>({SpatialMode::World, {}});
    actor.set<LocalTranslation>({0, 0, 6});
    check(std::abs(energy_at(0) / nominal - 1) < .004, "World source direction inherited parent");
    actor.set<SpatialBinding>(
        {SpatialMode::Explicit, f.engine.world().reference(f.scene.entity(parent).id()).value()});
    actor.set<LocalTranslation>({0, 0, 0});
    check(std::abs(energy_at(0) / nominal - .0625) < .004,
          "Explicit source space ignored referenced parent direction");
    actor.set<LocalScale>({1, 1, 0});
    check(energy_at(0) < 1e-10 && f.audio->status().at("failed_sources") == 0,
          "Degenerate emitter direction was not muted without destroying its voice");
    actor.set<LocalScale>({1, 1, 1});
    check(std::abs(energy_at(0) / nominal - .0625) < .004,
          "Fixed emitter direction did not recover without a config restart");
    config.cone_enabled = false;
    check(std::abs(energy_at(0) / nominal - 1) < .004,
          "Disabled direction still attenuated while retaining valid cone settings");
    config.spatialized = false;
    config.cone_enabled = true;
    const auto nonspatial = energy_at(0);
    check(std::abs(energy_at(179) / nonspatial - 1) < .004,
          "Nonspatial source still used its cone");
    const auto authored = f.scene.snapshot();
    for (const auto& [field, value] :
         std::array<std::pair<const char*, Json>, 6>{{{"inner_angle", -1},
                                                      {"outer_angle", 361},
                                                      {"outer_gain", -0.1},
                                                      {"outer_gain", 1.1},
                                                      {"cone_enabled", 1},
                                                      {"outer_angle", 30}}}) {
        reject([&] {
            authoring_command(f.scene, "property.set",
                              {{"entity", "speaker"},
                               {"component", "forge.audio_source"},
                               {"field", field},
                               {"value", value}});
        });
        check(f.scene.snapshot() == authored, "Invalid authored cone changed scene");
    }
    config.outer_angle = 20;
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 0, "Invalid native cone order entered mixer");
    config.outer_angle = 120;
    config.outer_gain = std::numeric_limits<float>::quiet_NaN();
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 0, "Nonfinite cone gain entered mixer");
    const auto checks = f.audio->status().at("configuration_checks").get<std::uint64_t>();
    for (unsigned i = 0; i < 8; ++i)
        f.tick();
    check(f.audio->status().at("configuration_checks") == checks,
          "Unchanged NaN native configuration was revalidated every game tick");
    config.outer_gain = .25;
    actor.set(config);
    f.tick();
    check(f.audio->status().at("voices") == 1, "Corrected cone did not recover admission");
    check(f.audio->status().at("configuration_checks") == checks + 1,
          "Corrected native configuration did not retry exactly once");
    Fixture uninterrupted(root), changing(root);
    for (auto* world : {&uninterrupted, &changing}) {
        world->load(source(clip));
        listener_at_origin(*world);
        auto c = world->scene.entity("speaker").get<AudioSource>();
        c.spatialized = true;
        c.rolloff = 0;
        c.outer_gain = 1;
        world->scene.entity("speaker").set(c);
        world->scene.entity("speaker").set<LocalTranslation>({0, 0, 6});
        world->tick();
        world->simulation.audio_paused(false);
    }
    for (unsigned i = 0; i < 16; ++i) {
        auto c = changing.scene.entity("speaker").get<AudioSource>();
        c.cone_enabled = i % 2 != 0;
        changing.scene.entity("speaker").set(c);
        changing.scene.entity("speaker").set(rotation(i * 17));
        uninterrupted.tick();
        changing.tick();
        std::array<float, 512> expected{}, actual{};
        uninterrupted.audio->read_offline(expected);
        changing.audio->read_offline(actual);
        check(expected == actual, "Cone/direction edit reset playing sample cursor");
    }
}
