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
void opacity_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    const auto original = source.document;
    source.enable_gpu_opacity(false);
    check(source.document == original, "Absent GPU opacity disable changed a legacy source");
    source.enable_opacity(true);
    source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .25);
    source.set_collision(true);
    source.set_graph_program({{AssetId::generate()}, {3, 4, 5}, 2}, false);
    const auto retained = source.document;
    source.enable_gpu_opacity(true);
    check(source.document.at("version") == 12 &&
              source.document.at("gpu_weighted_enabled") == true &&
              source.document.at("gpu_opacity_enabled") == true,
          "GPU opacity failed explicit source/weighted admission");
    for (const auto* field : {"asset_id", "mesh", "material", "program", "opacity_enabled",
                              "opacity_start", "opacity_end", "opacity_curve", "collision"})
        check(source.document.at(field) == retained.at(field),
              "GPU opacity lost retained settings or IDs");
    for (bool graph : {false, true}) {
        source.set_evaluation(graph ? ParticleEvaluation::Graph : ParticleEvaluation::Gpu);
        for (bool billboard : {false, true}) {
            source.enable_gpu_billboards(billboard);
            const auto active = source.document;
            const auto data = source.compiled();
            check(data.version == 16u + unsigned(graph) + 2u * unsigned(billboard) &&
                      particle_uses_gpu(data) && particle_uses_gpu_opacity(data) &&
                      particle_uses_opacity(data) && particle_uses_weighted_alpha(data) &&
                      particle_uses_graph(data) == graph &&
                      particle_uses_billboards(data) == billboard &&
                      !particle_uses_sorted_alpha(data) && !data.collision && !data.trail,
                  "GPU opacity projected another profile or inactive CPU behavior");
            check(decode_particle_effect(encode_particle_effect(data)) == data &&
                      decode_cooked_particle_effect(source.cook(), source.asset(), 3) == data,
                  "GPU opacity codecs lost the explicit value profile");
            const auto packet = prepare_particle_gpu_opacity_values(data);
            check(packet.count == data.opacity_curve.size() &&
                      packet.endpoints[0] == data.opacity_start &&
                      packet.endpoints[1] == data.opacity_end,
                  "GPU opacity packet lost authored appearance");
            reject([&] { (void)sample_particle_effect(data, .5, 1); });
            source.enable_gpu_opacity(true);
            source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
            check(source.document == active,
                  "Repeated GPU opacity/curve enable changed source or backend");
            const auto inserted =
                source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .75);
            check(source.compiled().version == data.version,
                  "GPU opacity stop insertion selected CPU");
            source.remove_lifetime_stop(ParticleLifetimeChannel::Opacity, inserted);
            check(source.document == active, "GPU stop insertion/removal lost source identities");
            source.enable_gpu_opacity(false);
            const auto plain = source.compiled();
            check(plain.version == data.version - 4 && !particle_uses_opacity(plain),
                  "GPU opacity disable changed weighted projection or leaked CPU opacity");
            reject([&] { (void)prepare_particle_gpu_opacity_values(plain); });
            source.enable_gpu_opacity(true);
            check(source.document == active, "GPU opacity round trip lost source settings");
            source.enable_gpu_weighted_alpha(false);
            check(source.document.at("version") == 12 &&
                      !particle_uses_opacity(source.compiled()) &&
                      source.document.at("gpu_opacity_enabled") == true,
                  "Weighted disable downgraded source or applied inactive GPU opacity");
            source.enable_gpu_weighted_alpha(true);
            check(source.document == active, "Weighted toggle lost retained GPU opacity");
            source.set_evaluation(ParticleEvaluation::CpuAlpha);
            source.enable_opacity(false);
            source.set_evaluation(graph ? ParticleEvaluation::Graph : ParticleEvaluation::Gpu);
            check(source.compiled().opacity_curve == data.opacity_curve,
                  "Inactive CPU enable state changed GPU appearance");
            source.enable_opacity(true);
            source.set_evaluation(graph ? ParticleEvaluation::Graph : ParticleEvaluation::Gpu);
            check(source.document == active, "CPU/GPU opacity round trip lost source settings");
            auto invalid = source;
            invalid.document["gpu_opacity_enabled"] = false;
            reject([&] { invalid.validate(); });
            invalid = source;
            invalid.document["version"] = 11;
            reject([&] { invalid.validate(); });
        }
    }
    source.set_evaluation(ParticleEvaluation::CpuAlpha);
    source.document["opacity_curve"] = nlohmann::json::array();
    for (double time : {0., .5, std::nextafter(.5, 1.), 1.})
        source.document["opacity_curve"].push_back({{"id", AssetId::generate()},
                                                    {"time", time},
                                                    {"value", .5},
                                                    {"interpolation", "linear"}});
    source.validate();
    const auto precise_cpu = source.document;
    reject([&] { source.set_evaluation(ParticleEvaluation::Gpu); });
    check(source.document == precise_cpu, "Rejected GPU float stops changed CPU source precision");
    source.enable_gpu_opacity(false);
    source.set_evaluation(ParticleEvaluation::Gpu);
    const auto inactive = source.document;
    reject([&] { source.enable_gpu_opacity(true); });
    check(source.document == inactive &&
              source.document.at("opacity_curve") == precise_cpu.at("opacity_curve"),
          "Inactive GPU opacity acquired float restrictions or rejected edit changed source");
}
void materials() {
    MaterialResourceData material;
    material.values.model = "forge.gltf.unlit.v1";
    material.values.alpha = MaterialAlpha::Blend;
    material.values.depth_write = false;
    for (unsigned version : {12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u}) {
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
        opacity_profiles();
        materials();
        std::cout
            << "Explicit weighted GPU profiles, retained identities and strict admission passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
