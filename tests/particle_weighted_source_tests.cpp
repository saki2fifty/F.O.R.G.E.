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
void collision_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    const auto original = source.document;
    source.set_gpu_collision(false);
    check(source.document == original, "Absent GPU plane disable changed a legacy source");
    source.enable_opacity(true);
    source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .25);
    source.set_trails(true);
    const ParticleCollisionPlane plane{{1, 2, -.5}, -2, .25, .7, .3, 5};
    source.set_collision(true, plane);
    source.set_graph_program({{AssetId::generate()}, {3, 4, 5}, 2}, false);
    source.document["extensions"] = {{"label", "retained plane settings"}};
    const auto retained = source.document;
    source.set_gpu_collision(true);
    check(source.document.at("version").get<unsigned>() == 13 && source.compiled().version == 20,
          "GPU plane source/value admission failed");
    for (const auto* field : {"asset_id", "mesh", "material", "program", "opacity_enabled",
                              "opacity_start", "opacity_end", "opacity_curve", "collision",
                              "collision_enabled", "trail", "trail_enabled", "extensions"})
        check(source.document.at(field) == retained.at(field),
              "GPU plane edit lost shared settings or inactive identities");
    for (unsigned version : {20u, 21u, 22u}) {
        auto variant = source;
        if (version >= 21)
            variant.enable_gpu_weighted_alpha(true);
        if (version == 22)
            variant.enable_gpu_opacity(true);
        const auto active = variant.document;
        const auto data = variant.compiled();
        check(
            data.version == version && particle_uses_gpu(data) && particle_uses_collision(data) &&
                data.collision == plane && particle_uses_weighted_alpha(data) == (version >= 21) &&
                particle_uses_opacity(data) == (version == 22) && !particle_uses_graph(data) &&
                !particle_uses_billboards(data) && !particle_uses_sorted_alpha(data) && !data.trail,
            "GPU plane projection leaked another motion or appearance backend");
        check(decode_particle_effect(encode_particle_effect(data)) == data &&
                  decode_cooked_particle_effect(variant.cook(), variant.asset(), 3) == data,
              "GPU plane value/envelope codec lost its profile");
        const auto text = active.dump();
        check(ParticleEffectSource::parse(std::as_bytes(std::span(text))).document == active,
              "GPU plane source round trip lost identities");
        reject([&] { (void)sample_particle_effect(data, .5, 1); });
        const auto values = prepare_particle_gpu_values(data, .5, 3, {}, CameraView{});
        auto plain = data;
        plain.version = version == 20 ? 3 : version == 21 ? 12 : 16;
        plain.collision.reset();
        const auto prior = prepare_particle_gpu_values(plain, .5, 3, {}, CameraView{});
        check(values.births == prior.births && values.modes == prior.modes &&
                  values.timing == prior.timing && values.emitter == prior.emitter &&
                  values.colors == prior.colors && values.tint == prior.tint,
              "GPU plane mode changed existing birth or appearance inputs");
        variant.set_gpu_collision(true);
        check(variant.document == active, "Repeated GPU plane enable changed source");
        variant.set_gpu_collision(false);
        check(variant.compiled() == plain, "GPU plane disable lost retained appearance");
        variant.set_gpu_collision(true);
        check(variant.document == active, "GPU plane toggle lost source identity");
        variant.set_evaluation(ParticleEvaluation::CpuAlpha);
        check(variant.compiled().version == 11 && variant.compiled().collision == plane,
              "GPU plane selection overwrote independent CPU activation");
        variant.set_evaluation(ParticleEvaluation::Graph);
        check(!variant.compiled().collision && particle_uses_graph(variant.compiled()) &&
                  variant.document.at("gpu_collision_enabled") == true,
              "Graph selection applied fake collision or lost inactive plane choice");
        variant.set_evaluation(ParticleEvaluation::Gpu);
        check(variant.document == active, "Backend round trip lost GPU plane settings");
        variant.enable_gpu_billboards(true);
        check(particle_uses_billboards(variant.compiled()) && !variant.compiled().collision &&
                  variant.document.at("gpu_collision_enabled") == false,
              "GPU alignment admitted a conflicting plane motion");
        variant.set_gpu_collision(true);
        check(variant.document == active, "Explicit GPU plane activation lost retained appearance");
        for (const auto& invalid : {nlohmann::json(), nlohmann::json(0), nlohmann::json("true")}) {
            auto wrong = variant;
            wrong.document["gpu_collision_enabled"] = invalid;
            reject([&] { wrong.validate(); });
        }
        auto wrong = variant;
        wrong.document["gpu_collision_enabled"] = false;
        reject([&] { wrong.validate(); });
        wrong = variant;
        wrong.document["gpu_billboard_enabled"] = true;
        reject([&] { wrong.validate(); });
        auto encoded = encode_particle_effect(data);
        encoded["version"] = 19;
        reject([&] { (void)decode_particle_effect(encoded); });
        encoded = encode_particle_effect(data);
        encoded["collision"]["bounce_limit"] = 17;
        reject([&] { (void)decode_particle_effect(encoded); });
        const auto previous = variant.document;
        auto invalid_plane = plane;
        invalid_plane.normal = {0, 0, 0};
        reject([&] { variant.set_gpu_collision(true, invalid_plane); });
        check(variant.document == previous, "Rejected GPU plane edit mutated source");
    }
    source.set_evaluation(ParticleEvaluation::CpuAlpha);
    source.document["opacity_curve"] = nlohmann::json::array();
    for (double time : {0., .5, std::nextafter(.5, 1.), 1.})
        source.document["opacity_curve"].push_back({{"id", AssetId::generate()},
                                                    {"time", time},
                                                    {"value", .5},
                                                    {"interpolation", "linear"}});
    source.validate();
    const auto precise = source.document.at("opacity_curve");
    source.set_gpu_collision(true);
    check(source.compiled().version == 20 && source.document.at("opacity_curve") == precise,
          "Inactive CPU opacity acquired GPU float restrictions");
    const auto active = source.document;
    reject([&] { source.enable_gpu_opacity(true); });
    check(source.document == active, "Rejected GPU plane opacity edit changed source precision");
}
void trail_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    const auto original = source.document;
    source.set_gpu_trails(false);
    check(source.document == original, "Disabled absent GPU trail mode changed legacy source");
    source.enable_opacity(true);
    source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .4);
    source.set_trails(true, ParticleTrailSettings{.75, 4});
    source.set_collision(true);
    source.set_graph_program({{AssetId::generate()}, {3, 4, 5}, 2}, false);
    source.set_gpu_collision(true);
    source.document["extensions"] = {{"label", "retained"}};
    const auto retained = source.document;
    source.set_gpu_trails(true);
    check(source.document.at("version").get<unsigned>() == 14 && source.compiled().version == 23 &&
              !source.document.at("gpu_collision_enabled").get<bool>() &&
              !source.document.at("gpu_billboard_enabled").get<bool>(),
          "GPU trail activation lost its explicit geometry/source profile");
    for (const auto* field :
         {"asset_id", "mesh", "material", "program", "opacity_enabled", "opacity_curve",
          "collision", "collision_enabled", "trail", "trail_enabled", "extensions"})
        check(source.document.at(field) == retained.at(field),
              "GPU trail activation changed retained settings or identities");
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto candidate = source;
        if (mode)
            candidate.enable_gpu_weighted_alpha(true);
        if (mode == 2)
            candidate.enable_gpu_opacity(true);
        const auto active = candidate.document;
        const auto data = candidate.compiled();
        check(data.version == 23 + mode && data.mesh == engine_primitive(8) &&
                  data.trail == ParticleTrailSettings{.75, 4} && !data.program && !data.collision &&
                  particle_uses_gpu(data) && particle_uses_trails(data) &&
                  !particle_uses_sorted_alpha(data) &&
                  particle_uses_weighted_alpha(data) == bool(mode) &&
                  particle_uses_gpu_opacity(data) == (mode == 2) &&
                  decode_particle_effect(encode_particle_effect(data)) == data &&
                  decode_cooked_particle_effect(candidate.cook(), candidate.asset(), 3) == data,
              "GPU ribbon profile or cook projection failed");
        check(particle_instance_capacity(data) == data.capacity * data.trail->segments,
              "GPU trails did not reserve every segment");
        reject([&] { (void)sample_particle_effect(data, .5, 41); });
        candidate.set_gpu_trails(false);
        check(!candidate.compiled().trail && candidate.document.at("trail") == active.at("trail"),
              "GPU trail disable lost settings or applied inactive geometry");
        candidate.set_gpu_trails(true);
        check(candidate.document == active, "GPU trail enable round trip lost identities");
        candidate.set_evaluation(ParticleEvaluation::Graph);
        check(candidate.compiled().program && !candidate.compiled().trail,
              "Inactive GPU trail settings changed an authored trajectory");
        candidate.set_evaluation(ParticleEvaluation::CpuAlpha);
        check(candidate.compiled().version == 11 &&
                  candidate.document.at("version").get<unsigned>() == 14,
              "CPU selection lost retained plane state or downgraded source");
        candidate.set_evaluation(ParticleEvaluation::Gpu);
        check(candidate.document == active, "Backend round trip lost GPU trail state");
        candidate.enable_gpu_billboards(true);
        check(!candidate.compiled().trail, "GPU alignment kept ribbon geometry active");
        candidate.set_gpu_trails(true);
        candidate.set_gpu_collision(true);
        check(!candidate.compiled().trail && candidate.document.at("version").get<unsigned>() == 14,
              "GPU collision kept ribbon geometry active or downgraded source");
        candidate.set_gpu_trails(true);
        check(candidate.document == active, "Geometry round trip lost retained source settings");
        for (auto emission : {ParticleEmission::Burst, ParticleEmission::Continuous})
            for (double time : {0., .125, 1., 2., 2.5, 2.75, 1e9, 1.}) {
                auto input = data;
                input.emission = emission;
                auto reference = input;
                reference.version = 2;
                reference.trail.reset();
                reference.opacity_start = reference.opacity_end = 1;
                reference.opacity_curve.clear();
                reference.lifetime += input.trail->duration;
                const auto samples = sample_particle_effect(reference, time, 41);
                const auto values = prepare_particle_gpu_values(input, time, 41, {}, {});
                check(values.births[0] == samples.size() &&
                          values.timing[2] == float(input.lifetime),
                      "GPU trail birth window lost tails or changed original life");
                if (!samples.empty()) {
                    const auto first = (std::uint64_t(values.births[2]) << 32) | values.births[1];
                    check(first == samples.front().birth &&
                              first + values.births[0] - 1 == samples.back().birth,
                          "GPU trail window changed stable birth ordinals");
                }
            }
        MaterialResourceData material;
        material.values.model = "forge.gltf.unlit.v1";
        material.values.double_sided = true;
        material.values.alpha = mode ? MaterialAlpha::Blend : MaterialAlpha::Opaque;
        material.values.depth_write = !mode;
        validate_particle_material_profile(material, data);
        material.values.double_sided = false;
        reject([&] { validate_particle_material_profile(material, data); });
    }
    auto long_tail = source.compiled();
    long_tail.capacity = 512;
    long_tail.lifetime = 30;
    long_tail.spawn_rate = 8;
    long_tail.trail = ParticleTrailSettings{30, 16};
    for (double time : {0., 15., 30., 45., 60., 1e9}) {
        const auto values = prepare_particle_gpu_values(long_tail, time, 41, {}, {});
        check(values.births[0] == unsigned(std::min(480., std::floor(time * 8))) &&
                  values.timing[2] == 30 && values.timing[0] <= 60,
              "GPU trail maximum window exceeded short-age/capacity admission");
    }
    for (const auto& invalid : {nlohmann::json(), nlohmann::json(1), nlohmann::json("true")}) {
        auto candidate = source;
        candidate.document["gpu_trail_enabled"] = invalid;
        reject([&] { candidate.validate(); });
    }
    for (const auto* field : {"gpu_collision_enabled", "gpu_billboard_enabled"}) {
        auto candidate = source;
        candidate.document[field] = true;
        reject([&] { candidate.validate(); });
    }
    const auto unchanged = source.document;
    for (auto settings :
         {ParticleTrailSettings{0, 4}, ParticleTrailSettings{31, 4}, ParticleTrailSettings{.5, 0},
          ParticleTrailSettings{.5, 17}, ParticleTrailSettings{30, 16}}) {
        reject([&] { source.set_gpu_trails(true, settings); });
        check(source.document == unchanged, "Rejected GPU trail edit changed authored source");
    }
}
void materials() {
    MaterialResourceData material;
    material.values.model = "forge.gltf.unlit.v1";
    material.values.alpha = MaterialAlpha::Blend;
    material.values.depth_write = false;
    for (unsigned version : {12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 21u, 22u}) {
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
    for (unsigned version : {3u, 4u, 8u, 9u, 20u}) {
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
        collision_profiles();
        trail_profiles();
        materials();
        std::cout
            << "Explicit weighted GPU profiles, retained identities and strict admission passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
