#pragma once
#include "mesh_uv_surface.hpp"
#include <forge/material_resource.hpp>

namespace forge {
struct MeshTextureSurfaceTarget {
    AssetRef<TextureAsset> texture;
    TextureSemantic semantic = TextureSemantic::Color;
    std::string slot = "baseColorTexture";
};
// Transient data coordinates from one retained or immutable published selection.
// The renderer/source document remain the resource, gesture and history owners.
struct MeshTextureSurfacePoint {
    Double2 uv{};
    std::array<Double2, 3> triangle_uv{};
    std::array<std::uint32_t, 3> indices{};
    std::size_t primitive = 0;
};
MeshTextureSurfacePoint mesh_texture_surface_point(const MeshPart&, const MaterialResourceData&,
                                                   const MeshTextureSurfaceTarget&,
                                                   const MeshPickTriangle&, MeshPickBudget&);
bool mesh_texture_surface_connected(const MeshTextureSurfacePoint&, const MeshTextureSurfacePoint&);
struct MeshTextureSurfaceHit {
    EntityId entity;
    AssetId mesh, material;
    std::string mesh_revision, mesh_variant, material_revision, material_variant;
    unsigned lod = 0, part = 0;
    MeshTextureSurfacePoint point;
    bool same_selection(const MeshTextureSurfaceHit&) const;
};
} // namespace forge
