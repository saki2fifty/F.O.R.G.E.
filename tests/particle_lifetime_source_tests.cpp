#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/particle_effect_source.hpp>
#include <iostream>
#include <limits>
using namespace forge;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Malformed particle source accepted");
}
ParticleEffectSource parse(const nlohmann::json& document) {
    const auto text = document.dump();
    return ParticleEffectSource::parse(std::as_bytes(std::span(text)));
}
void legacy_and_promotion() {
    ParticleEffectData data;
    data.mesh = engine_primitive(0);
    auto legacy = ParticleEffectSource{encode_particle_effect(data)};
    legacy.document["asset_id"] = AssetId::generate();
    legacy.document["extensions"] = {{"author", {{"note", "original test"}}}};
    const auto original = legacy.document;
    legacy = parse(original);
    require(legacy.compiled() == data, "Legacy source reinterpreted its values");
    auto cooked = decode_cooked_particle_effect(legacy.cook(), legacy.asset());
    require(cooked.version == 2 && legacy.document == original,
            "Legacy cooking failed to normalize or changed the authored source");
    for (const auto seed : {0u, 7u, 0xffffffffu})
        require(sample_particle_effect(cooked, .9, seed) == sample_particle_effect(data, .9, seed),
                "Normalized legacy cooking changed sampling");
    // Existing immutable v1 cooks remain admitted without rewriting their bytes.
    auto old_cook = encode_particle_effect(data);
    old_cook["format"] = particle_effect_cooked_format;
    old_cook["asset_id"] = legacy.asset();
    const auto text = old_cook.dump();
    require(decode_cooked_particle_effect(std::as_bytes(std::span(text)), legacy.asset()) == data,
            "Old immutable particle cook rejected");
    legacy.promote();
    require(legacy.document.at("version") == 2 && legacy.document.at("profile") == "cpu_mesh2" &&
                legacy.document.at("extensions") == original.at("extensions") &&
                legacy.document.at("asset_id") == original.at("asset_id"),
            "Promotion lost source identity or opaque metadata");
    const auto promoted = legacy.document;
    legacy.promote();
    require(legacy.document == promoted, "Repeated promotion changed the source");
}
void backend_profiles_and_envelopes() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.document["extensions"] = {{"label", "retained"}};
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    source.enable_lifetime(ParticleLifetimeChannel::Color, true);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .25);
    const auto cpu = source.document;
    source.set_evaluation(ParticleEvaluation::Gpu);
    auto expected = cpu;
    expected["version"] = 3;
    expected["profile"] = "gpu_mesh1";
    for (const nlohmann::json& version :
         {nlohmann::json(3.), nlohmann::json(true), nlohmann::json(-1),
          nlohmann::json((std::uint64_t{1} << 32) + 3)}) {
        auto invalid = source;
        invalid.document["version"] = version;
        reject([&] { invalid.validate(); });
    }
    require(source.document == expected, "GPU selection changed source UUIDs, stops or extensions");
    source.promote();
    require(source.document == expected, "Extended-field promotion changed the GPU profile");
    reject([&] { source.set_evaluation(ParticleEvaluation(-1)); });
    require(source.document == expected, "Rejected evaluation changed source data");
    const auto gpu_values = source.compiled();
    require(gpu_values.version == 3 &&
                decode_particle_effect(encode_particle_effect(gpu_values)) == gpu_values,
            "GPU value codec failed its explicit version/profile");
    reject([&] { (void)sample_particle_effect(gpu_values, .5, 1); });
    require(decode_cooked_particle_effect(source.cook(), source.asset(), 3) == gpu_values,
            "GPU cooked envelope lost its value profile");
    reject([&] { (void)decode_cooked_particle_effect(source.cook(), source.asset(), 2); });
    auto wrapper = nlohmann::json::parse(source.cook());
    require(wrapper.at("version") == 3 && !wrapper.at("values").contains("extensions"),
            "GPU envelope shipped opaque source metadata");
    wrapper["version"] = 3.0;
    auto bytes = wrapper.dump();
    reject([&] {
        (void)decode_cooked_particle_effect(std::as_bytes(std::span(bytes)), source.asset());
    });
    wrapper["version"] = 3;
    wrapper["extra"] = true;
    bytes = wrapper.dump();
    reject([&] {
        (void)decode_cooked_particle_effect(std::as_bytes(std::span(bytes)), source.asset());
    });
    source.set_evaluation(ParticleEvaluation::Cpu);
    require(source.document == cpu, "Returning to CPU changed source identity or values");
    const auto cpu_values = source.compiled();
    require(decode_cooked_particle_effect(source.cook(), source.asset(), 3) == cpu_values,
            "Neutral cooked envelope changed CPU values");
    auto legacy = encode_particle_effect(cpu_values);
    legacy["format"] = particle_effect_cooked_format;
    legacy["asset_id"] = source.asset();
    bytes = legacy.dump();
    require(decode_cooked_particle_effect(std::as_bytes(std::span(bytes)), source.asset(), 2) ==
                cpu_values,
            "Existing immutable v2 artifact was rejected");
    reject([&] {
        (void)decode_cooked_particle_effect(std::as_bytes(std::span(bytes)), source.asset(), 3);
    });
    source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .5);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Size, std::nextafter(.5, 1.));
    const auto close_times = source.document;
    reject([&] { source.set_evaluation(ParticleEvaluation::Gpu); });
    require(source.document == close_times, "GPU float-time collision changed the CPU source");
}
void lifetime_identity_and_projection() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.document["size_start"] = .2;
    source.document["size_end"] = .8;
    source.document["color_start"] = {1, 0, 0};
    source.document["color_end"] = {0, 0, 1};
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    source.enable_lifetime(ParticleLifetimeChannel::Color, true);
    const auto enabled = source.document;
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    require(source.document == enabled, "Enabling an enabled channel replaced its identities");
    const auto inserted = source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .25);
    const auto color = source.insert_lifetime_stop(ParticleLifetimeChannel::Color, .5);
    require(source.document.at("size_curve")[1].at("id").get<AssetId>() == inserted &&
                std::abs(source.compiled().size_curve[1].value - .35) < 1e-12 &&
                source.compiled().color_gradient[1].value == std::array{.5f, 0.f, .5f},
            "Inserted source stop did not retain interpolation or identity");
    source.document["size_curve"][0]["interpolation"] = "step";
    const auto stepped = source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .125);
    require(source.document.at("size_curve")[1].at("value") == .2 &&
                source.document.at("size_curve")[1].at("interpolation") == "step",
            "Inserted Step stop changed the outgoing segment");
    const auto saved = source.document;
    auto reopened = parse(saved);
    require(reopened.document == saved, "Roundtrip changed local particle stop UUIDs");
    // Local identities belong to the root, so a duplicated root retains local UUIDs.
    reopened.document["asset_id"] = AssetId::generate();
    reopened.validate();
    require(reopened.document.at("size_curve") == saved.at("size_curve"),
            "A different root invalidated its independent local stop identities");
    const auto cooked = source.cook();
    const auto projection = nlohmann::json::parse(cooked.begin(), cooked.end());
    for (const auto* name : {"size_curve", "color_gradient"})
        for (const auto& stop : projection.at("values").at(name))
            require(!stop.contains("id"), "Cooked evaluator retained a source-only stop UUID");
    require(decode_cooked_particle_effect(cooked, source.asset()) == source.compiled(),
            "Cooked lifetime values differ from source projection");
    auto invalid = saved;
    invalid["color_gradient"][0]["id"] = saved.at("size_curve")[0].at("id");
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][0]["id"] = source.asset();
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][0].erase("id");
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][0]["id"] = "invalid UUID";
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["size_curve"][1]["time"] = 1;
    reject([&] { parse(invalid); });
    invalid = saved;
    invalid["color_gradient"][0]["unknown"] = true;
    reject([&] { parse(invalid); });
    const auto unchanged = source.document;
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .25); });
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Size, 0); });
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Color, NAN); });
    reject([&] { source.remove_lifetime_stop(ParticleLifetimeChannel::Size, color); });
    reject([&] {
        source.remove_lifetime_stop(ParticleLifetimeChannel::Size,
                                    source.document.at("size_curve")[0].at("id").get<AssetId>());
    });
    require(source.document == unchanged, "Rejected lifetime mutation changed its source");
    source.remove_lifetime_stop(ParticleLifetimeChannel::Size, stepped);
    source.remove_lifetime_stop(ParticleLifetimeChannel::Size, inserted);
    source.enable_lifetime(ParticleLifetimeChannel::Size, false);
    require(source.compiled().size_curve.empty() && source.compiled().size_start == .2 &&
                source.compiled().size_end == .8 && !source.compiled().color_gradient.empty(),
            "Disabling a size curve lost fallback values or changed the color channel");
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    for (unsigned i = 1; i < 15; ++i)
        source.insert_lifetime_stop(ParticleLifetimeChannel::Size, double(i) / 16);
    require(source.compiled().size_curve.size() == 16, "Maximum stop count failed");
    const auto full = source.document;
    reject([&] { source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .99); });
    require(source.document == full, "Excess stop rejected after mutating the source");
}
} // namespace
void graph_program_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.document["extensions"] = {{"future", {{"opaque", source.asset()}}}};
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    const auto before = source.document;
    reject([&] { source.set_evaluation(ParticleEvaluation::Graph); });
    require(source.document == before, "Missing graph selection partially changed source");
    ParticleGraphProgram program;
    program.shader.id = AssetId::generate();
    program.extent = {2, 3, 4};
    program.maximum_size = .8;
    source.set_graph_program(program);
    require(source.document.at("version") == 4 && source.compiled().version == 4 &&
                source.compiled().program == program &&
                source.document.at("asset_id") == before.at("asset_id") &&
                source.document.at("size_curve") == before.at("size_curve") &&
                source.document.at("extensions") == before.at("extensions"),
            "Graph assignment lost source identity, stops, settings or opaque data");
    const auto graph = source.document;
    const auto values = source.compiled();
    require(decode_particle_effect(encode_particle_effect(values)) == values &&
                decode_cooked_particle_effect(source.cook(), source.asset(), 3) == values,
            "Graph values/cooked projection did not roundtrip");
    const auto text = source.document.dump();
    require(ParticleEffectSource::parse(std::as_bytes(std::span(text))).document == graph,
            "Particle graph source did not roundtrip");
    reject([&] { (void)sample_particle_effect(values, .5, 7); });
    for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu}) {
        source.set_evaluation(backend);
        const auto active = source.compiled();
        require(source.document.at("version") == 4 && !active.program &&
                    active.version == (backend == ParticleEvaluation::Cpu ? 2u : 3u) &&
                    source.document.at("program") == graph.at("program") &&
                    source.document.at("size_curve") == graph.at("size_curve") &&
                    decode_cooked_particle_effect(source.cook(), source.asset(), 3) == active,
                "Backend switch lost inactive graph settings or cooked an inactive program");
        source.set_evaluation(ParticleEvaluation::Graph);
        require(source.document == graph && source.compiled() == values,
                "Returning to Graph did not restore the exact authored program");
    }
    auto invalid = program;
    invalid.shader.id = {};
    reject([&] { source.set_graph_program(invalid); });
    for (double value : {-1., 1e7, std::numeric_limits<double>::infinity(), 1e-300}) {
        invalid = program;
        invalid.extent[0] = value;
        reject([&] { source.set_graph_program(invalid); });
    }
    invalid = program;
    invalid.maximum_size = 10001;
    reject([&] { source.set_graph_program(invalid); });
    require(source.document == graph, "Rejected graph bounds partially changed the source");
    auto data = values;
    data.version = 3;
    reject([&] { validate_particle_effect(data); });
    auto malformed = encode_particle_effect(values);
    malformed["program"]["opaque"] = 1;
    reject([&] { (void)decode_particle_effect(malformed); });
    source.set_evaluation(ParticleEvaluation::Cpu);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .5);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Size, std::nextafter(.5, 1.));
    require(source.compiled().version == 2 && source.compiled().size_curve.size() == 4,
            "Inactive graph imposed GPU float-stop restrictions on CPU evaluation");
    source.set_graph_program(program, false);
    require(source.compiled().version == 2 && source.graph_program() == program,
            "Inactive program edit changed the CPU evaluation backend");
    const auto precise_cpu = source.document;
    reject([&] { source.set_evaluation(ParticleEvaluation::Graph); });
    require(source.document == precise_cpu,
            "Rejected GPU precision transition changed the retained CPU graph source");
}
void sorted_alpha_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.enable_lifetime(ParticleLifetimeChannel::Size, true);
    source.document["extensions"] = {{"original", "preserve"}};
    const auto original = source.document;
    const auto cpu = source.compiled();
    source.set_evaluation(ParticleEvaluation::CpuAlpha);
    const auto alpha = source.compiled();
    require(source.document.at("version") == 5 && alpha.version == 5 &&
                source.document.at("profile") == "cpu_alpha1" && !particle_uses_gpu(alpha) &&
                source.document.at("asset_id") == original.at("asset_id") &&
                source.document.at("size_curve") == original.at("size_curve") &&
                source.document.at("extensions") == original.at("extensions"),
            "Sorted-alpha selection lost CPU ownership, identity or source settings");
    require(decode_particle_effect(encode_particle_effect(alpha)) == alpha &&
                decode_cooked_particle_effect(source.cook(), source.asset(), 3) == alpha,
            "Sorted-alpha source/cooked values did not roundtrip");
    const auto text = source.document.dump();
    require(ParticleEffectSource::parse(std::as_bytes(std::span(text))).document == source.document,
            "Sorted-alpha source did not roundtrip");
    for (double time : {0., .125, .5, 1., 3., 1e9})
        require(sample_particle_effect(alpha, time, 31) == sample_particle_effect(cpu, time, 31),
                "Sorted-alpha evaluation changed CPU birth, random or motion semantics");
    ParticleGraphProgram program;
    program.shader.id = AssetId::generate();
    source.set_graph_program(program, false);
    const auto retained = source.document;
    for (auto backend : {ParticleEvaluation::Cpu, ParticleEvaluation::Gpu,
                         ParticleEvaluation::Graph, ParticleEvaluation::CpuAlpha}) {
        source.set_evaluation(backend);
        const auto active = source.compiled();
        require(source.document.at("version") == 5 && source.graph_program() == program &&
                    active.version == (backend == ParticleEvaluation::Cpu     ? 2u
                                       : backend == ParticleEvaluation::Gpu   ? 3u
                                       : backend == ParticleEvaluation::Graph ? 4u
                                                                              : 5u) &&
                    decode_cooked_particle_effect(source.cook(), source.asset(), 3) == active,
                "Source5 backend switch lost retained program or active value profile");
    }
    require(source.document == retained,
            "Returning to sorted-alpha evaluation did not restore the complete source");
    source.insert_lifetime_stop(ParticleLifetimeChannel::Size, .5);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Size, std::nextafter(.5, 1.));
    const auto precise = source.document;
    reject([&] { source.set_evaluation(ParticleEvaluation::Gpu); });
    require(source.document == precise && source.compiled().version == 5,
            "GPU precision refusal changed retained CPU alpha stops");
    auto invalid = encode_particle_effect(alpha);
    invalid["profile"] = "gpu_mesh1";
    reject([&] { (void)decode_particle_effect(invalid); });
    invalid = encode_particle_effect(alpha);
    invalid["version"] = 4;
    reject([&] { (void)decode_particle_effect(invalid); });
}
void opacity_profiles() {
    auto source = ParticleEffectSource::create(AssetId::generate());
    ParticleGraphProgram program;
    program.shader.id = AssetId::generate();
    source.set_graph_program(program, false);
    source.set_evaluation(ParticleEvaluation::CpuAlpha);
    const auto before = source.document;
    source.enable_opacity(false);
    require(source.document == before, "Disabling absent opacity changed older source bytes");
    source.enable_opacity(true);
    auto data = source.compiled();
    require(data.version == 6 && !particle_uses_gpu(data) && particle_uses_sorted_alpha(data) &&
                source.document.at("version").get<unsigned>() == 6 &&
                source.document.at("asset_id") == before.at("asset_id") &&
                source.document.at("profile").get<std::string>() == "cpu_alpha2" &&
                data.opacity_start == 1 && data.opacity_end == 0,
            "Opacity activation lost CPU ownership, identity or default fade");
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 1;
    data.lifetime = 4;
    for (const auto time : {0., .5, 1., 2., 3.5, std::nextafter(4., 0.)}) {
        auto values = sample_particle_effect(data, time, 42);
        require(values.size() == 1 && values.front().opacity == 1 - time / 4,
                "Start/end opacity differs from normalized lifetime interpolation");
        auto original = data;
        original.version = 5;
        original.opacity_end = 1;
        values.front().opacity = 1;
        require(values == sample_particle_effect(original, time, 42),
                "Opacity changed birth membership, motion, size or RGB");
    }
    require(sample_particle_effect(data, 4, 42).empty(), "Opacity resurrected a dead birth");
    data.opacity_curve = {{0, 0, false}, {.5, 1, true}, {1, 0, false}};
    for (auto [time, expected] : {std::pair{0., 0.}, {1., .5}, {2., 1.}, {3., 1.}})
        require(sample_particle_effect(data, time, 42).front().opacity == expected,
                "Opacity Step/Linear boundaries differ from independent expected values");
    require(decode_particle_effect(encode_particle_effect(data)) == data,
            "Opacity runtime values did not roundtrip");
    auto invalid = data;
    invalid.opacity_curve[1].value = std::numeric_limits<double>::quiet_NaN();
    reject([&] { validate_particle_effect(invalid); });
    invalid = data;
    invalid.opacity_curve[1].time = 0;
    reject([&] { validate_particle_effect(invalid); });
    invalid = data;
    invalid.opacity_end = 1.01;
    reject([&] { validate_particle_effect(invalid); });
    invalid = data;
    invalid.version = 5;
    reject([&] { validate_particle_effect(invalid); });
    auto document = encode_particle_effect(data);
    document["opacity_curve"][0]["interpolation"] = "cubic";
    reject([&] { (void)decode_particle_effect(document); });
    source.enable_lifetime(ParticleLifetimeChannel::Opacity, true);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, .5);
    source.insert_lifetime_stop(ParticleLifetimeChannel::Opacity, std::nextafter(.5, 1.));
    const auto retained = source.document;
    for (auto backend : {ParticleEvaluation::Gpu, ParticleEvaluation::Cpu,
                         ParticleEvaluation::Graph, ParticleEvaluation::CpuAlpha}) {
        source.set_evaluation(backend);
        require(source.document.at("opacity_curve") == retained.at("opacity_curve") &&
                    source.document.at("opacity_enabled").get<bool>() &&
                    source.graph_program() == program &&
                    source.compiled().version == (backend == ParticleEvaluation::Gpu     ? 3u
                                                  : backend == ParticleEvaluation::Cpu   ? 2u
                                                  : backend == ParticleEvaluation::Graph ? 4u
                                                                                         : 6u),
                "Backend transition lost inactive CPU opacity or applied GPU precision to it");
        require(decode_cooked_particle_effect(source.cook(), source.asset(), 3) ==
                    source.compiled(),
                "Source6 cooked projection retained inactive opacity fields");
    }
    require(source.document == retained, "Returning to opacity changed retained source/stop IDs");
    source.enable_opacity(false);
    require(source.compiled().version == 5 &&
                source.document.at("opacity_curve") == retained.at("opacity_curve"),
            "Disabling opacity lost retained stops or changed the older alpha projection");
    source.enable_opacity(true);
    require(source.document == retained, "Re-enabling opacity did not restore source bytes");
    document = source.document;
    document["opacity_enabled"] = false;
    reject([&] { ParticleEffectSource{document}.validate(); });
    document = source.document;
    document["opacity_curve"][1]["id"] = document["opacity_curve"][0]["id"];
    reject([&] { ParticleEffectSource{document}.validate(); });
}
int main() {
    try {
        graph_program_profiles();
        sorted_alpha_profiles();
        opacity_profiles();
        legacy_and_promotion();
        backend_profiles_and_envelopes();
        lifetime_identity_and_projection();
        std::cout << "Legacy normalization, stable local stops and bounded projection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
