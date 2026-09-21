#pragma once
#include "material_shader.hpp"
#include "mesh_vertex_fetch.hpp"
namespace forge {
inline constexpr unsigned mesh_draw_light_limit = 64;
struct MeshDrawShader {
    std::string vertex, pixel;
    MaterialShader material;
    bool sheen{};
};
// Pure source/binding preparation; no device, pipeline or source importer.
MeshDrawShader mesh_draw_shader(const MeshVertexFetch&, const PbrMaterialProfile&);
} // namespace forge
