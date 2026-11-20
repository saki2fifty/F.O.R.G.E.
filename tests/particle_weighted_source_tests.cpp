#include "particle_gpu_plan.hpp"
#include "particle_render_instances.hpp"
#include <forge/engine_assets.hpp>
#include <forge/particle_effect_source.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid weighted particle candidate admitted");
}
void profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    const auto original = source.document;
    source.enable_gpu_weighted_alpha(false);
    check(source.document == original, "Absent disabled weighted mode changed a legacy source");
    source.enable_opacity(true);
    source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .4);
    source.set_trails(true);
    source.set_collision(true, ParticleCollisionPlane{{1, 2, 3}, -2, .1, .7, .3, 5});
    source.set_graph_program({{AssetId::generate()}, {3, 4, 5}, 2}, false);
    source.document["extensions"] = {{"label", "preserve"}};
    const auto retained = source.document;
    source.enable_gpu_weighted_alpha(true);
    check(source.document.at("version") == 11 && source.compiled().version == 12,
          "Weighted source/value projection failed");
    for (const auto* field :
         {"asset_id", "mesh", "material", "program", "billboard_enabled", "gpu_billboard_enabled",
          "opacity_enabled", "opacity_curve", "collision", "collision_enabled", "trail",
          "trail_enabled", "extensions"})
        check(source.document.at(field) == retained.at(field),
              "Weighted edit lost retained settings");
    for (bool graph : {false, true}) {
        source.set_evaluation(graph ? ParticleEvaluation::Graph : ParticleEvaluation::Gpu);
        for (bool billboard : {false, true}) {
            source.enable_gpu_billboards(billboard);
            const auto data = source.compiled();
            check(data.version == 12u + unsigned(graph) + 2u * unsigned(billboard) &&
                      particle_uses_gpu(data) && particle_uses_weighted_alpha(data) &&
                      particle_uses_graph(data) == graph &&
                      particle_uses_billboards(data) == billboard &&
                      !particle_uses_sorted_alpha(data) && !particle_uses_opacity(data) &&
                      !data.collision && !data.trail && data.opacity_curve.empty(),
                  "Weighted values leaked another profile's settings");
            check(decode_particle_effect(encode_particle_effect(data)) == data &&
                      decode_cooked_particle_effect(source.cook(), source.asset(), 3) == data,
                  "Weighted value/envelope codecs lost their profile");
            reject([&] { (void)sample_particle_effect(data, .5, 1); });
            const auto values = prepare_particle_gpu_values(data, .5, 3, {}, CameraView{});
            auto old = data;
            old.version = graph ? (billboard ? 9 : 4) : (billboard ? 8 : 3);
            const auto previous = prepare_particle_gpu_values(old, .5, 3, {}, CameraView{});
            check(values.births == previous.births && values.modes == previous.modes &&
                      values.timing == previous.timing && values.emitter == previous.emitter &&
                      values.colors == previous.colors && values.tint == previous.tint,
                  "Weighted mode changed existing evaluator inputs");
            const auto active = source.document;
            source.enable_gpu_weighted_alpha(true);
            check(source.document == active, "Repeated weighted selection is not a no-op");
            source.enable_gpu_weighted_alpha(false);
            check(source.compiled().version == old.version,
                  "Disabling weighted mode lost GPU selection");
            source.enable_gpu_weighted_alpha(true);
            check(source.document == active, "Weighted round trip lost source values");
            source.set_evaluation(ParticleEvaluation::CpuAlpha);
            check(source.compiled().version == 11 &&
                      source.document.at("gpu_weighted_enabled") == true,
                  "CPU selection lost retained weighted mode or collision settings");
            source.set_collision(false);
            check(source.document.at("version") == 11 && source.compiled().version == 10,
                  "Collision toggle downgraded source or lost retained ribbons");
            source.set_collision(true);
            source.set_evaluation(graph ? ParticleEvaluation::Graph : ParticleEvaluation::Gpu);
            check(source.document == active, "CPU round trip lost weighted source identity");
            for (const auto& invalid :
                 {nlohmann::json(), nlohmann::json(0), nlohmann::json("true")}) {
                auto wrong = source;
                wrong.document["gpu_weighted_enabled"] = invalid;
                reject([&] { wrong.validate(); });
            }
            auto wrong = source;
            wrong.document["gpu_weighted_enabled"] = false;
            reject([&] { wrong.validate(); });
            auto encoded = encode_particle_effect(data);
            encoded["opacity_start"] = .5;
            reject([&] { (void)decode_particle_effect(encoded); });
        }
    }
}
void materials() {
    MaterialResourceData material;
    material.values.model = "forge.gltf.unlit.v1";
    material.values.alpha = MaterialAlpha::Blend;
    material.values.depth_write = false;
    for (unsigned version : {12u, 13u, 14u, 15u}) {
        ParticleEffectData data;
        data.version = version;
        validate_particle_material_profile(material, data);
        for (const auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Mask}) {
            auto wrong = material;
            wrong.values.alpha = alpha;
            reject([&] { validate_particle_material_profile(wrong, data); });
        }
        auto wrong = material;
        wrong.values.depth_write = true;
        reject([&] { validate_particle_material_profile(wrong, data); });
        wrong = material;
        wrong.values.depth_test = false;
        reject([&] { validate_particle_material_profile(wrong, data); });
    }
    for (unsigned version : {3u, 4u, 8u, 9u}) {
        ParticleEffectData data;
        data.version = version;
        reject([&] { validate_particle_material_profile(material, data); });
    }
}
} // namespace
int main() {
    try {
        profiles();
        materials();
        std::cout
            << "Explicit weighted GPU profiles, retained identities and strict admission passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
