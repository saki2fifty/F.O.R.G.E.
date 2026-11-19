#pragma once
#include "particle_gpu_plan.hpp"
#include "shader_publication.hpp"
#include <cstddef>
#include <forge/material_graph.hpp>
namespace forge::particle_graph_shader_tests {
using Json = nlohmann::json;
inline void check(bool ok) {
    if (!ok)
        throw std::runtime_error("Particle graph Shader profile check failed");
}
template <class F> void rejects(F action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid particle graph Shader admitted");
}
inline void run(const ShaderData& opaque) {
    // CPU envelope/layout fixtures only; these opaque bytes never reach a device.
    auto graph = opaque;
    graph.particle_graph_profile = particle_graph_profile_version;
    auto& stage = graph.stages.front();
    stage.stage = ShaderStage::Compute;
    stage.entry = "main";
    Json variables = Json::array();
    const auto append = [&](const char* name, std::size_t offset, unsigned count,
                            const char* basic) {
        variables.push_back({{"name", name},
                             {"class", "vector"},
                             {"basic", basic},
                             {"rows", 1},
                             {"columns", 4},
                             {"offset", offset},
                             {"array_size", count},
                             {"members", Json::array()}});
    };
#define FORGE_TEST_PARTICLE_FIELD(Name, Member, Count, Basic)                                      \
    append(Name, offsetof(ParticleGpuValues, Member), Count, Basic)
    FORGE_TEST_PARTICLE_FIELD("Births", births, 0, "uint");
    FORGE_TEST_PARTICLE_FIELD("Modes", modes, 0, "uint");
    FORGE_TEST_PARTICLE_FIELD("Timing", timing, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Axis", axis, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Side", side, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Up", up, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Acceleration", acceleration, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Shape", shape, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Sizes", sizes, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Tint", tint, 0, "float");
    FORGE_TEST_PARTICLE_FIELD("Emitter", emitter, 3, "float");
    FORGE_TEST_PARTICLE_FIELD("Colors", colors, 2, "float");
    FORGE_TEST_PARTICLE_FIELD("SizeStops", size_stops, 16, "float");
    FORGE_TEST_PARTICLE_FIELD("ColorStops", color_stops, 16, "float");
#undef FORGE_TEST_PARTICLE_FIELD
    Json values{{"name", "ParticleValues"},
                {"kind", "constant_buffer"},
                {"register", 1},
                {"space", 0},
                {"array_size", 1},
                {"dimension", "unknown"},
                {"size", sizeof(ParticleGpuValues)},
                {"variables", variables}};
    variables = Json::array();
    append("GraphExtentSize", 0, 0, "float");
    Json limits{{"name", "ParticleGraphLimits"},
                {"kind", "constant_buffer"},
                {"register", 0},
                {"space", 0},
                {"array_size", 1},
                {"dimension", "unknown"},
                {"size", sizeof(ParticleGpuGraphLimits)},
                {"variables", variables}};
    Json output{{"name", "ParticleRows"}, {"kind", "buffer_uav"}, {"register", 0}, {"space", 0},
                {"array_size", 1},        {"dimension", "buffer"}};
    stage.reflection = {
        {"stage", "compute"}, {"threads", {64, 1, 1}}, {"resources", {values, limits, output}}};
    validate_particle_graph_shader(graph);
    const auto bytes = encode_shader(graph);
    const auto loaded = decode_shader(bytes);
    check(loaded.particle_graph_profile == 1 && encode_shader(loaded) == bytes);
    const auto metadata = asset_detail::shader_publication_metadata(graph);
    asset_detail::validate_shader_publication_metadata(metadata, loaded);
    auto ordinary = graph;
    ordinary.particle_graph_profile = 0;
    validate_shader(ordinary);
    rejects([&] { validate_particle_graph_shader(ordinary); });
    check(ordinary.layout_digest() != graph.layout_digest());
    auto missing = metadata;
    missing.erase("particle_graph_profile");
    rejects([&] { asset_detail::validate_shader_publication_metadata(missing, graph); });
    missing = metadata;
    missing["particle_graph_profile"] = 1.0;
    rejects([&] { asset_detail::validate_shader_publication_metadata(missing, graph); });
    rejects([&] { asset_detail::validate_shader_publication_metadata(metadata, ordinary); });
    for (const auto* field : {"size", "variables", "space", "name"}) {
        auto invalid = graph;
        invalid.stages[0].reflection["resources"][0].erase(field);
        rejects([&] { validate_shader(invalid); });
    }
    auto invalid = graph;
    invalid.stages[0].reflection["threads"] = {32, 1, 1};
    rejects([&] { validate_shader(invalid); });
    invalid = graph;
    invalid.stages[0].reflection["resources"][0]["variables"][0]["offset"] = 16;
    rejects([&] { validate_shader(invalid); });
    auto source = MaterialGraphSource::create_particle(AssetId::generate());
    auto program = shader_program_source(source.document);
    check(program.particle_graph_profile == 1 && !program.surface);
    program.particle_graph_profile = 2;
    rejects([&] { validate_shader_program(program); });
    program = shader_program_source(source.document);
    program.defines["OVERRIDE"] = "1";
    rejects([&] { validate_shader_program(program); });
}
} // namespace forge::particle_graph_shader_tests
