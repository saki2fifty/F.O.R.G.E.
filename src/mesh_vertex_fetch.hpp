#pragma once
#include "mesh_gpu.hpp"
#include "pbr_material.hpp"
namespace forge {
struct MeshVertexFetch {
    // Native ByteAddressBuffer g_MeshVertices + ForgeLoadMeshVertex(uint vertexId).
    std::string source;
    // Dense transient slots mapped from persistent material UV-set semantics.
    std::vector<unsigned> uv_sets;
    bool normal{}, tangent{}, color{}, skin{}, triangles{};
    unsigned morph_count{};
    // Packed byte offset / scalar width for POSITION, NORMAL, TANGENT, COLOR_0
    // and selected UV channels, four slots per row. UINT32_MAX is absent.
    std::vector<std::array<std::uint32_t, 4>> morph_offsets;
};
// Generated shader adapter for admitted GPU resources. No input-layout truncation
// or conversion of integer joints. The caller supplies a zero-based indexed draw.
MeshVertexFetch mesh_vertex_fetch(const GpuMeshPart&, const PbrMaterialProfile&);
} // namespace forge
