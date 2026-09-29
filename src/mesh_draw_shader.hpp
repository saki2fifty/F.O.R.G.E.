#pragma once
#include "material_shader.hpp"
#include "mesh_shader_input.hpp"
#include <forge/surface_shader.hpp>
namespace forge {
inline constexpr unsigned mesh_draw_light_limit = 64;
struct MeshGeometryShader {
    std::string vertex, geometry, varyings;
    bool instanced{};
};
// Both built-in PBR and custom pixel surfaces use the same engine-owned
// geometry, morphing, skinning and winding implementation.
MeshGeometryShader mesh_geometry_shader(const MeshVertexFetch&, bool allow_instances = true);
struct MeshDrawShader {
    std::string vertex, pixel;
    MaterialShader material;
    bool sheen{}, transmission{};
    std::string geometry;
    bool instanced{};
};
// Pure source/binding preparation; no device, pipeline or source importer.
MeshDrawShader mesh_draw_shader(const MeshVertexFetch&, const PbrMaterialProfile&,
                                bool shadow_pass = false,
                                MaterialSamplerBinding = MaterialSamplerBinding::Array,
                                const SurfaceShaderDefinition* graph = nullptr,
                                std::string_view graph_source = {});
std::string material_graph_shader_wrapper(const SurfaceShaderDefinition&, std::string_view source,
                                          bool depth_only);
} // namespace forge
