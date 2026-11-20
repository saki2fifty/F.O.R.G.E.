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
} // namespace
int main() {
    try {
        profiles();
        std::cout << "Scene particle source profiles passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
