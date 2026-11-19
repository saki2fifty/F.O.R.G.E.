#pragma once
#include "mesh_instance_layout.hpp"
#include "particle_gpu_shaders.hpp"
#include <forge/material_graph.hpp>
namespace forge {
// Compiler-owned kernel, compiled/cooked by the ordinary bounded Shader worker.
// Authored graph documents never supply shader text or another resource layout.
inline std::string particle_graph_compute_source(const nlohmann::json& graph) {
    return "#define FORGE_PARTICLE_GRAPH 1\n#define FORGE_STATIC_INSTANCE_ROWS " +
           std::to_string(mesh_static_instance_rows) + R"(
#include "ForgeMath.fxh"
struct ForgeParticleGraphInput {
    float3 Position,Velocity,Origin;
    float Age,Age01,Size;
    float3 Color;
    float Random;
};
struct ForgeParticleGraphValues {float3 Position;float Size;float3 Color;};
cbuffer ParticleGraphLimits {float4 GraphExtentSize;};
)" + compile_particle_graph(graph).source +
           particle_gpu_compute;
}
} // namespace forge
