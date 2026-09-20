#pragma once
#include <forge/mesh_asset.hpp>
#include <map>
#include <stop_token>

namespace forge::asset_detail {
enum class MeshDirections { Preserve, GenerateMissing, Recalculate };
struct MeshProcessingOptions {
    // glTF missing normals mean flat shading, including each morph target.
    MeshDirections normals = MeshDirections::GenerateMissing;
    MeshDirections tangents = MeshDirections::GenerateMissing;
    bool weld_exact = true;
    bool optimize_vertex_fetch = true;
    // Reordering triangles is only valid for an order-independent draw. Material
    // admission must opt in per slot; transparent draws retain source order.
    std::vector<std::uint32_t> order_independent_material_slots;
    // Normal-map UV selection by material slot; defaults to TEXCOORD_0.
    std::map<std::uint32_t, unsigned> tangent_uv_sets;
};
struct ProcessedMesh {
    MeshData mesh;
    std::vector<std::string> diagnostics;
};
// Private offline preparation; all streams, integer joints and every morph stream
// participate in remapping. Failure leaves the caller's admitted mesh unchanged.
ProcessedMesh process_mesh(const MeshData& source, const MeshProcessingOptions& options = {},
                           MeshLimits limits = {}, std::stop_token cancel = {});
} // namespace forge::asset_detail
