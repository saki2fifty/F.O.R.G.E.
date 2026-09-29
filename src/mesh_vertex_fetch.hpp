#pragma once
#include "mesh_gpu.hpp"
#include "mesh_shader_input.hpp"
#include "pbr_material.hpp"
namespace forge {
// Generated shader adapter for admitted GPU resources. No input-layout truncation
// or conversion of integer joints. The caller supplies a zero-based indexed draw.
MeshVertexFetch mesh_vertex_fetch(const GpuMeshPart&, const PbrMaterialProfile&,
                                  bool enable_skin = true);
// Preserve the surface declaration's dense order; UV-set numbers remain logical
// semantics, never hardware register indices.
MeshVertexFetch mesh_vertex_fetch(const GpuMeshPart&, std::span<const unsigned> uv_sets,
                                  bool enable_skin = true);
} // namespace forge
