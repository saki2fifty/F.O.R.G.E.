#pragma once
#include "decal_shader.hpp"
#include "mesh_draw_shader.hpp"
#include <limits>
inline void test_decal_shader_profile() {
    using namespace forge;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    const auto reject = [&](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Invalid decal receiver reflection was accepted");
    };
    for (std::size_t samplers = 0; samplers < 65; ++samplers) {
        const auto capacity = decal_shader_capacity(samplers, true);
        check(capacity <= 8 && (!capacity || samplers + 2 + capacity <= 16),
              "Decal bindings exceed the portable stage sampler budget");
        check(capacity == (samplers <= 6   ? 8
                           : samplers < 14 ? 14 - samplers
                                           : 0) &&
                  !decal_shader_capacity(samplers, false),
              "Decal capacity narrows old materials or admits unlit receivers");
    }
    check(!decal_shader_capacity(std::numeric_limits<std::size_t>::max(), true),
          "Large sampler count wrapped the receiver budget");
    MaterialData values;
    values.model = "forge.gltf.metallic-roughness.v1";
    MeshVertexFetch fetch;
    fetch.normal = fetch.tangent = true;
    fetch.uv_sets = {0};
    const auto profile = prepare_pbr_material(values);
    const auto old = mesh_draw_shader(fetch, profile);
    const auto projected =
        mesh_draw_shader(fetch, profile, false, MaterialSamplerBinding::Array, nullptr, {}, false,
                         {}, false, false, false, false, true);
    const auto normal_at = projected.pixel.find("float3 geometric=n;");
    const auto projection_at = projected.pixel.find("ForgeApplyProjectedSurface(input.World,");
    const auto brdf_at = projected.pixel.find("GetSurfaceReflectanceMR(base.rgb");
    check(!old.decal_capacity && old.pixel.find("ForgeDecals.fxh") == std::string::npos &&
              projected.decal_capacity == 8 &&
              projected.pixel.find("#define FORGE_DECAL_CAPACITY 8\n") != std::string::npos &&
              brdf_at != std::string::npos && normal_at < projection_at &&
              projection_at < brdf_at && projected.vertex == old.vertex &&
              projected.geometry == old.geometry &&
              projected.material.uniforms == old.material.uniforms,
          "Color projection bypasses the geometric-normal or pre-BRDF stage");
    SurfaceShaderDefinition graph;
    graph.physically_based = true;
    graph.uv_sets = {0};
    for (unsigned samplers = 0; samplers <= 19; ++samplers) {
        graph.textures.clear();
        for (unsigned i = 0; i < samplers; ++i)
            graph.textures.emplace("color" + std::to_string(i), MaterialTextureSlot{});
        const auto plain = material_graph_shader_wrapper(graph, "graph.hlsli", false);
        const auto projected_graph = material_graph_shader_wrapper(
            graph, "graph.hlsli", false, false, false, false, false, true);
        const auto capacity = decal_shader_capacity(samplers, true);
        check(capacity ? projected_graph.find("#define FORGE_DECAL_CAPACITY " +
                                              std::to_string(capacity) + "\n") != std::string::npos
                       : projected_graph == plain,
              "Graph projection ignores declared samplers or changes a full old program");
        const auto depth = material_graph_shader_wrapper(graph, "graph.hlsli", true);
        check(depth == material_graph_shader_wrapper(graph, "graph.hlsli", true, false, false,
                                                     false, false, true),
              "Projected color changes receiver shadow coverage");
    }
    using Json = nlohmann::json;
    graph.textures.clear();
    Json reflection{
        {"stage", "pixel"},
        {"resources",
         Json::array({{{"name", "ForgeDecals"},
                       {"kind", "constant_buffer"},
                       {"array_size", 1},
                       {"size", 65 * 16},
                       {"variables", Json::array({{{"name", "g_ForgeDecals"},
                                                   {"offset", 0},
                                                   {"basic", "float"},
                                                   {"class", "vector"},
                                                   {"rows", 1},
                                                   {"columns", 4},
                                                   {"array_size", 65},
                                                   {"members", Json::array()}}})}},
                      {{"name", "g_ForgeDecalSamplers"}, {"kind", "sampler"}, {"array_size", 8}},
                      {{"name", "g_ForgeDecalColor7"},
                       {"kind", "texture_srv"},
                       {"array_size", 1},
                       {"dimension", "texture2d"}}})}};
    (void)surface_binding_layout(graph, reflection, false, true);
    reject([&] { surface_binding_layout(graph, reflection); });
    auto bad = reflection;
    bad["resources"][0]["variables"][0]["array_size"] = 64;
    reject([&] { surface_binding_layout(graph, bad, false, true); });
    bad = reflection;
    bad["resources"][1]["array_size"] = 7;
    reject([&] { surface_binding_layout(graph, bad, false, true); });
    bad = reflection;
    bad["resources"][2]["name"] = "g_ForgeDecalColor8";
    reject([&] { surface_binding_layout(graph, bad, false, true); });
    bad["resources"][2]["name"] = "g_ForgeDecalColor07";
    reject([&] { surface_binding_layout(graph, bad, false, true); });
    auto channels = reflection;
    channels["resources"][0]["size"] = 105 * 16;
    channels["resources"][0]["variables"][0]["array_size"] = 105;
    channels["resources"][2]["name"] = "g_ForgeDecalMap7";
    (void)surface_binding_layout(graph, channels, false, true, true);
    reject([&] { surface_binding_layout(graph, channels, false, true); });
    reject([&] { surface_binding_layout(graph, reflection, false, true, true); });
    graph.physically_based = false;
    reject([&] { surface_binding_layout(graph, reflection, false, true); });
}
