#include "particle_path.hpp"
#include <forge/engine_assets.hpp>
#include <forge/particle_effect_source.hpp>
#include <iostream>
#include <limits>

using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Action> void reject(Action&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid Scene particle source was admitted");
}
ParticleEffectSource parse(const nlohmann::json& value) {
    const auto bytes = value.dump();
    return ParticleEffectSource::parse(std::as_bytes(std::span(bytes)));
}
void xy_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.document["extensions"] = {{"scene_collision_2d", "opaque retained data"}};
    const auto old = source.document;
    source.set_scene_collision_space(false);
    check(source.document == old, "Selecting the existing 3D space upgraded legacy source");
    source.set_scene_collision_space(true);
    check(source.document.at("version").get<unsigned>() == 18 && source.compiled().version == 2 &&
              !source.compiled().scene_collision &&
              source.document.at("extensions") == old.at("extensions") &&
              source.document.at("asset_id") == old.at("asset_id"),
          "Inactive XY selection changed the active backend, opaque data or identity");
    source.set_scene_collision(true);
    check(source.compiled().version == 52 && particle_uses_scene_collision_2d(source.compiled()) &&
              particle_uses_scene_collision(source.compiled()),
          "XY Mesh profile was not projected explicitly");
    source.enable_billboards(true);
    check(source.compiled().version == 53 && particle_uses_billboards(source.compiled()),
          "XY camera-plane geometry did not compose through the existing profile owner");
    const auto active = source.document;
    for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu}) {
        source.set_evaluation(backend);
        check(!source.compiled().scene_collision &&
                  source.document.at("scene_collision_2d").get<bool>(),
              "Inactive XY settings leaked into another backend or were discarded");
        source.set_evaluation(ParticleEvaluation::CpuAlpha);
        check(source.document == active, "Backend switching lost retained XY settings or IDs");
    }
    source.set_scene_collision_space(false);
    check(source.compiled().version == 39 && !particle_uses_scene_collision_2d(source.compiled()),
          "Returning to 3D reinterpreted the existing profile");
    source.set_scene_collision_space(true);
    check(source.document == active && parse(active).document == active &&
              decode_cooked_particle_effect(source.cook(), source.asset(), 3) == source.compiled(),
          "XY source/cooked round trips changed settings, profiles or identities");
    auto malformed = active;
    malformed["scene_collision_2d"] = 1;
    reject([&] { (void)parse(malformed); });
    malformed = active;
    malformed["version"] = 17;
    reject([&] { (void)parse(malformed); });
    for (double radius : {.001, 500.001}) {
        auto settings = *source.compiled().scene_collision;
        settings.radius = radius;
        reject([&] { source.set_scene_collision(true, settings); });
        check(source.document == active, "Invalid native circle radius changed admitted source");
    }
    source.set_scene_collision_space(false);
    auto settings = *source.compiled().scene_collision;
    settings.radius = .001;
    source.set_scene_collision(true, settings);
    const auto three_d = source.document;
    reject([&] { source.set_scene_collision_space(true); });
    check(source.document == three_d && source.compiled().version == 39,
          "Unsupported XY radius silently clamped or changed the previous 3D source");
    settings.radius = .05;
    source.set_scene_collision(true, settings, true);
    check(source.compiled().version == 53 && source.compiled().scene_collision == settings,
          "Combined XY settings validated an intermediate unsupported 3D radius");
    settings.radius = .001;
    source.set_scene_collision(true, settings, false);
    check(source.document == three_d,
          "Combined 3D settings validated an intermediate unsupported XY radius or lost IDs");
    reject([&] { source.set_scene_collision(true, settings, true); });
    check(source.document == three_d, "Rejected combined settings partially changed source");
    source.set_evaluation(ParticleEvaluation::Gpu);
    settings.radius = .05;
    source.set_scene_collision(false, settings, true);
    const auto inactive = source.document;
    source.set_scene_collision_space(false);
    check(source.compiled().version == 3 &&
              !source.document.at("scene_collision_enabled").get<bool>() &&
              source.document.at("scene_collision") == inactive.at("scene_collision"),
          "Space-only selection activated retained CPU collision or changed GPU evaluation");
}
void world_trail_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.document["extensions"] = {{"world_trail_enabled", "opaque source data"}};
    const auto original = source.document;
    source.set_world_trails(false);
    check(source.document == original, "Inactive World trails upgraded an older source");
    source.enable_billboards(true);
    source.enable_opacity(true);
    source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
    const auto stop = source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .5);
    const auto retained = source.document;
    const ParticleTrailSettings settings{.5, 4};
    source.set_world_trails(true, settings);
    const auto world = source.document;
    check(
        world.at("version") == 19 && world.at("world_trail_enabled").get<bool>() &&
            world.at("scene_collision_enabled").get<bool>() &&
            !world.at("trail_enabled").get<bool>() && !world.at("collision_enabled").get<bool>() &&
            source.compiled().version == 54 && particle_uses_scene_trails(source.compiled()) &&
            source.compiled().mesh == engine_primitive(8) && source.compiled().trail == settings &&
            world.at("mesh") == retained.at("mesh") &&
            world.at("opacity_curve") == retained.at("opacity_curve") &&
            world.at("asset_id") == original.at("asset_id") &&
            world.at("extensions") == original.at("extensions"),
        "World trails lost their explicit profile, retained Mesh/appearance or source identities");
    source.set_world_trails(true, settings);
    check(source.document == world, "Repeated World trail activation was not a no-op");
    source.set_scene_collision_space(true);
    const auto xy = source.document;
    check(xy.at("version") == 19 && source.compiled().version == 55 &&
              particle_uses_scene_collision_2d(source.compiled()) &&
              source.compiled().trail == settings,
          "World trail query-space change downgraded the source or lost its settings");
    for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu}) {
        source.set_evaluation(backend);
        check(!source.compiled().trail && !source.compiled().scene_collision &&
                  source.document.at("world_trail_enabled").get<bool>(),
              "Inactive World trail settings leaked into another backend or were discarded");
        source.set_evaluation(ParticleEvaluation::CpuAlpha);
        check(source.document == xy, "Backend changes lost World trail source state or IDs");
    }
    check(parse(xy).document == xy &&
              decode_cooked_particle_effect(source.cook(), source.asset(), 3) == source.compiled(),
          "World trail source/cooked round trips changed its explicit profile or identities");
    source.set_world_trails(false);
    check(source.compiled().version == 53 && !source.compiled().trail &&
              source.document.at("trail") == xy.at("trail"),
          "World trail disable lost retained settings or camera-plane head geometry");
    source.set_world_trails(true);
    check(source.document == xy, "World trail disable/enable did not restore exact source state");
    source.set_scene_collision(false);
    check(!source.compiled().scene_collision && !source.compiled().trail &&
              source.document.at("world_trail_enabled").get<bool>(),
          "Scene disable did not retain inactive World trail intent");
    source.set_scene_collision(true);
    check(source.document == xy, "Scene disable/enable lost historical trail intent");
    source.set_trails(true);
    check(source.compiled().version == 10 &&
              !source.document.at("world_trail_enabled").get<bool>() &&
              !source.document.at("scene_collision_enabled").get<bool>(),
          "Original local-trail operation changed its explicit activation semantics");
    source.set_world_trails(true);
    check(source.document == xy, "Returning from local trails lost World settings or identities");
    source.set_collision(true);
    check(source.compiled().version == 11 && !source.document.at("world_trail_enabled").get<bool>(),
          "Original local-plane operation did not deactivate the World trail geometry");
    source.set_world_trails(true);
    source.set_scene_collision_space(false);
    check(source.document == world, "World trail space round trip changed 3D settings or IDs");
    for (auto invalid : {ParticleTrailSettings{.5, 17}, ParticleTrailSettings{30, 16}}) {
        reject([&] { source.set_world_trails(true, invalid); });
        check(source.document == world, "Rejected World trail settings partially changed source");
    }
    for (unsigned kind = 0; kind < 4; ++kind) {
        auto bad = world;
        if (kind == 0)
            bad.erase("world_trail_enabled");
        if (kind == 1)
            bad["world_trail_enabled"] = 1;
        if (kind == 2)
            bad["world_trail_enabled"] = false;
        if (kind == 3)
            bad["version"] = 18;
        reject([&] { (void)parse(bad); });
    }
    source.remove_lifetime_stop(ParticleLifetimeChannel::Opacity, stop);
    check(source.document.at("opacity_curve").size() == 2,
          "World trails lost ordinary lifetime source editing");
}
void profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    const auto original = source.document;
    source.set_scene_collision(false);
    check(source.document == original, "Absent Scene collision changed legacy source bytes");
    source.enable_billboards(true);
    source.enable_opacity(true);
    source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
    const auto stop = source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .5);
    source.set_trails(true);
    source.set_collision(true);
    const auto retained = source.document;
    ParticleSceneCollision settings{.025, .75, .25, 8, 0x80000001u};
    source.set_scene_collision(true, settings);
    const auto scene = source.document;
    check(scene.at("version") == 16 && source.compiled().version == 39 &&
              source.compiled().scene_collision == settings && !source.compiled().collision &&
              !source.compiled().trail && !scene.at("collision_enabled").get<bool>() &&
              !scene.at("trail_enabled").get<bool>() &&
              scene.at("collision") == retained.at("collision") &&
              scene.at("trail") == retained.at("trail") &&
              scene.at("opacity_curve") == retained.at("opacity_curve") &&
              scene.at("asset_id") == original.at("asset_id"),
          "Scene response lost retained settings/IDs or selected incompatible geometry");
    check(parse(scene).document == scene &&
              decode_cooked_particle_effect(source.cook(), source.asset(), 3) == source.compiled(),
          "Scene particle source/cooked round trip changed values or identity");
    reject([&] { (void)sample_particle_effect(source.compiled(), .5, 17); });
    reject([&] { (void)sample_particle_paths(source.compiled(), .5, 17, .5, 4); });
    source.enable_billboards(false);
    check(source.compiled().version == 38 && source.compiled().mesh == engine_primitive(0),
          "Scene Mesh projection did not retain the authored Mesh");
    source.enable_billboards(true);
    check(source.document == scene, "Camera-plane toggle changed Scene source state");
    source.enable_opacity(false);
    check(source.compiled().opacity_start == 1 && source.compiled().opacity_end == 1 &&
              source.compiled().opacity_curve.empty() &&
              source.compiled().scene_collision == settings,
          "Inactive Scene opacity leaked retained appearance into runtime values");
    source.enable_opacity(true);
    check(source.document == scene, "Scene opacity toggle lost retained local stop IDs");
    for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu}) {
        source.set_evaluation(backend);
        check(!source.compiled().scene_collision &&
                  source.document.at("scene_collision") == scene.at("scene_collision"),
              "Inactive Scene settings leaked into another evaluation backend");
        source.set_evaluation(ParticleEvaluation::CpuAlpha);
        check(source.document == scene, "Backend toggle did not restore the Scene CPU projection");
    }
    source.enable_gpu_sorted_alpha(true);
    check(source.document.at("version") == 16 && !source.compiled().scene_collision,
          "GPU sorted selection downgraded the source or inherited Scene response");
    source.set_evaluation(ParticleEvaluation::CpuAlpha);
    check(source.compiled().version == 39, "Scene selection was lost behind inactive GPU settings");
    source.set_collision(true);
    check(source.compiled().version == 11 &&
              !source.document.at("scene_collision_enabled").get<bool>(),
          "Explicit local-plane selection did not deactivate Scene response");
    source.set_scene_collision(true);
    source.set_trails(true);
    check(source.compiled().version == 10 &&
              !source.document.at("scene_collision_enabled").get<bool>(),
          "Explicit ribbon selection did not deactivate Scene response");
    source.set_scene_collision(true);
    source.remove_lifetime_stop(ParticleLifetimeChannel::Opacity, stop);
    check(source.document.at("opacity_curve").size() == 2,
          "Scene response lost ordinary lifetime editing");
    const auto unchanged = source.document;
    settings.radius = std::numeric_limits<double>::quiet_NaN();
    reject([&] { source.set_scene_collision(true, settings); });
    check(source.document == unchanged, "Invalid Scene settings changed the source candidate");
    for (unsigned kind = 0; kind < 7; ++kind) {
        auto invalid = scene;
        if (kind == 0)
            invalid["version"] = 15;
        if (kind == 1)
            invalid["scene_collision_enabled"] = 1;
        if (kind == 2)
            invalid["scene_collision_enabled"] = false;
        if (kind == 3)
            invalid["collision_enabled"] = true;
        if (kind == 4)
            invalid["trail_enabled"] = true;
        if (kind == 5)
            invalid["scene_collision"]["mask"] = -1;
        if (kind == 6)
            invalid["profile"] = "cpu_scene_collision1";
        reject([&] { (void)parse(invalid); });
    }
}
void impact_profiles() {
    for (bool planar : {false, true})
        for (unsigned geometry = 0; geometry < 3; ++geometry) {
            auto source = ParticleEffectSource::create(AssetId::generate());
            source.document["extensions"] = {{"scene_impacts_enabled", "opaque"}};
            const auto original = source.document;
            source.set_scene_impacts(false);
            check(source.document == original, "Inactive impacts upgraded a legacy source");
            if (geometry == 1)
                source.enable_billboards(true);
            source.set_scene_collision(true);
            source.set_scene_collision_space(planar);
            if (geometry == 2)
                source.set_world_trails(true, ParticleTrailSettings{.5, 4});
            const auto prior_version = source.compiled().version;
            source.set_scene_impacts(true);
            const auto active = source.document;
            const auto expected = geometry == 2 ? 60u + planar : 56u + geometry + 2u * planar;
            check(active.at("version").get<unsigned>() == 20 &&
                      source.compiled().version == expected &&
                      particle_uses_scene_impacts(source.compiled()) &&
                      active.at("asset_id") == original.at("asset_id") &&
                      active.at("extensions") == original.at("extensions") &&
                      parse(active).document == active &&
                      decode_cooked_particle_effect(source.cook(), source.asset(), 3) ==
                          source.compiled(),
                  "Scene impacts lost explicit profile, source identities or immutable cook");
            source.set_scene_impacts(true);
            check(source.document == active, "Repeated impact activation changed source state");
            for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu}) {
                source.set_evaluation(backend);
                check(!particle_uses_scene_impacts(source.compiled()) &&
                          source.document.at("scene_impacts_enabled").get<bool>(),
                      "Inactive impact intent leaked into another evaluation or was discarded");
                source.set_evaluation(ParticleEvaluation::CpuAlpha);
                check(source.document == active, "Backend switch lost retained impact source");
            }
            source.set_scene_impacts(false);
            check(source.compiled().version == prior_version &&
                      !particle_uses_scene_impacts(source.compiled()),
                  "Disabling impacts changed the retained Scene geometry");
            source.set_scene_impacts(true);
            check(source.document == active, "Impact disable/enable lost exact source state");
            source.set_scene_collision_space(!planar);
            check(source.document.at("version").get<unsigned>() == 20 &&
                      source.compiled().version ==
                          (geometry == 2 ? 60u + !planar : 56u + geometry + 2u * !planar),
                  "Impact query-space change downgraded source or discarded notifications");
            source.set_scene_collision_space(planar);
            check(source.document == active, "Impact query-space round trip changed source");
            if (geometry == 2) {
                source.set_world_trails(true, ParticleTrailSettings{.625, 8});
                check(source.document.at("version").get<unsigned>() == 20 &&
                          particle_uses_scene_impacts(source.compiled()) &&
                          source.compiled().trail == ParticleTrailSettings{.625, 8},
                      "World trail settings downgraded impact source20");
            }
            for (const auto& invalid :
                 {nlohmann::json(), nlohmann::json(1), nlohmann::json("true")}) {
                auto bad = active;
                bad["scene_impacts_enabled"] = invalid;
                reject([&] { (void)parse(bad); });
            }
        }
}
void rest_profiles() {
    constexpr unsigned expected[2][2][3] = {{{62, 63, 66}, {64, 65, 67}},
                                            {{68, 69, 72}, {70, 71, 73}}};
    for (bool impacts : {false, true})
        for (bool planar : {false, true})
            for (unsigned geometry = 0; geometry < 3; ++geometry) {
                auto source = ParticleEffectSource::create(AssetId::generate());
                source.document["extensions"] = {{"scene_resting_enabled", "opaque"}};
                const auto original = source.document;
                source.set_scene_resting(false);
                check(source.document == original, "Inactive resting upgraded legacy source");
                if (geometry == 1)
                    source.enable_billboards(true);
                source.set_scene_collision(true, ParticleSceneCollision{.05, 0, .25, 4, 3}, planar);
                if (geometry == 2)
                    source.set_world_trails(true, ParticleTrailSettings{.5, 4});
                if (impacts)
                    source.set_scene_impacts(true);
                const auto previous = source.compiled().version;
                source.set_scene_resting(true);
                const auto active = source.document;
                check(active.at("version").get<unsigned>() == 21 &&
                          source.compiled().version == expected[impacts][planar][geometry] &&
                          particle_uses_scene_resting(source.compiled()) &&
                          particle_uses_scene_impacts(source.compiled()) == impacts &&
                          active.at("asset_id") == original.at("asset_id") &&
                          active.at("extensions") == original.at("extensions") &&
                          parse(active).document == active &&
                          decode_cooked_particle_effect(source.cook(), source.asset(), 3) ==
                              source.compiled(),
                      "Resting profile lost explicit values, source/cook identity or prior intent");
                source.set_scene_resting(true);
                check(source.document == active, "Repeated resting activation changed source");
                for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu}) {
                    source.set_evaluation(backend);
                    check(!particle_uses_scene_resting(source.compiled()) &&
                              source.document.at("scene_resting_enabled").get<bool>(),
                          "Inactive resting intent leaked into another backend or was discarded");
                    source.set_evaluation(ParticleEvaluation::CpuAlpha);
                    check(source.document == active, "Resting backend round trip lost source");
                }
                source.set_scene_resting(false);
                check(source.compiled().version == previous,
                      "Disabling resting changed prior geometry or notifications");
                source.set_scene_resting(true);
                check(source.document == active, "Resting toggle lost retained source values");
                source.set_scene_impacts(!impacts);
                check(source.compiled().version == expected[!impacts][planar][geometry] &&
                          source.document.at("version").get<unsigned>() == 21,
                      "Notification change downgraded or disabled resting source");
                source.set_scene_impacts(impacts);
                source.set_scene_collision_space(!planar);
                check(source.compiled().version == expected[impacts][!planar][geometry],
                      "Query-space change discarded resting");
                source.set_scene_collision_space(planar);
                check(source.document == active,
                      "Resting query/notification round trip lost source");
                for (const auto& invalid :
                     {nlohmann::json(), nlohmann::json(1), nlohmann::json("true")}) {
                    auto wrong = active;
                    wrong["scene_resting_enabled"] = invalid;
                    reject([&] { (void)parse(wrong); });
                }
                auto wrong_version = active;
                wrong_version["version"] = 20;
                reject([&] { (void)parse(wrong_version); });
            }
}
void motion_profiles() {
    for (bool resting : {false, true})
        for (bool impacts : {false, true})
            for (bool planar : {false, true})
                for (unsigned geometry = 0; geometry < 3; ++geometry) {
                    auto source = ParticleEffectSource::create(AssetId::generate());
                    source.document["extensions"] = {{"scene_motion_enabled", "opaque"}};
                    const auto original = source.document;
                    source.set_scene_motion(false);
                    check(source.document == original,
                          "Inactive Scene motion upgraded legacy source");
                    source.set_scene_collision(true);
                    source.set_scene_collision_space(planar);
                    if (geometry == 1)
                        source.enable_billboards(true);
                    if (geometry == 2)
                        source.set_world_trails(true, ParticleTrailSettings{.5, 4});
                    if (impacts)
                        source.set_scene_impacts(true);
                    if (resting)
                        source.set_scene_resting(true);
                    const auto old_values = source.compiled();
                    source.set_scene_motion(true);
                    const auto active = source.document;
                    const auto geometry_index =
                        geometry == 2 ? 4u + planar : geometry + 2u * planar;
                    const auto expected = 74u + 12u * resting + 6u * impacts + geometry_index;
                    check(active.at("version").get<unsigned>() == 22 &&
                              source.compiled().version == expected &&
                              particle_uses_scene_motion(source.compiled()) &&
                              particle_uses_scene_resting(source.compiled()) == resting &&
                              particle_uses_scene_impacts(source.compiled()) == impacts &&
                              active.at("asset_id") == original.at("asset_id") &&
                              active.at("extensions") == original.at("extensions") &&
                              parse(active).document == active &&
                              decode_cooked_particle_effect(source.cook(), source.asset(), 3) ==
                                  source.compiled(),
                          "Scene motion lost explicit profile, intent, source or cooked identity");
                    source.set_scene_motion(true);
                    check(source.document == active,
                          "Repeated Scene motion selection changed source");
                    for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu}) {
                        source.set_evaluation(backend);
                        check(
                            !particle_uses_scene_motion(source.compiled()) &&
                                source.document.at("scene_motion_enabled").get<bool>(),
                            "Inactive Scene motion intent leaked into another backend or was lost");
                        source.set_evaluation(ParticleEvaluation::CpuAlpha);
                        check(source.document == active,
                              "Scene motion backend round trip lost source");
                    }
                    source.set_scene_motion(false);
                    check(source.compiled() == old_values,
                          "Disabling Scene motion changed earlier immutable values");
                    source.set_scene_motion(true);
                    source.set_scene_resting(!resting);
                    source.set_scene_impacts(!impacts);
                    source.set_scene_collision_space(!planar);
                    check(particle_uses_scene_motion(source.compiled()) &&
                              particle_uses_scene_resting(source.compiled()) == !resting &&
                              particle_uses_scene_impacts(source.compiled()) == !impacts &&
                              particle_uses_scene_collision_2d(source.compiled()) == !planar,
                          "Scene motion was discarded by composed source controls");
                    source.set_scene_resting(resting);
                    source.set_scene_impacts(impacts);
                    source.set_scene_collision_space(planar);
                    check(source.document == active,
                          "Scene motion settings round trip lost identity");
                    for (const auto& invalid :
                         {nlohmann::json(), nlohmann::json(1), nlohmann::json("true")}) {
                        auto wrong = active;
                        wrong["scene_motion_enabled"] = invalid;
                        reject([&] { (void)parse(wrong); });
                    }
                    auto wrong = active;
                    wrong["version"] = 21;
                    reject([&] { (void)parse(wrong); });
                    wrong = active;
                    wrong.erase("scene_resting_enabled");
                    reject([&] { (void)parse(wrong); });
                    wrong = active;
                    wrong["scene_motion_enabled"] = false;
                    reject([&] { (void)parse(wrong); });
                }
    auto legacy = ParticleEffectSource::create(AssetId::generate());
    legacy.set_scene_motion(true);
    check(legacy.compiled().version == 74 && !particle_uses_scene_resting(legacy.compiled()),
          "Direct legacy promotion omitted required inherited source fields");
}
} // namespace
int main() {
    try {
        profiles();
        xy_profiles();
        world_trail_profiles();
        impact_profiles();
        rest_profiles();
        motion_profiles();
        std::cout << "Scene particle source profiles passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
