#pragma once
#include "mesh_texture_surface.hpp"
#include "paint_texture_source.hpp"
#include <forge/assets.hpp>

namespace forge {
struct PaintMeshSelection {
    AssetRef<MeshAsset> mesh;
    AssetRef<MaterialAsset> material;
    std::string mesh_revision, material_revision, slot = "baseColorTexture";
    unsigned lod = 0;
};
struct PaintMeshPoint {
    unsigned part = 0;
    MeshPickTriangle triangle;
    bool break_before = false;
};
// Explicit barycentric samples in an immutable publication. Scene pointer input
// uses the renderer's current visible pose; both paths share Material/UV mapping.
std::vector<PaintPoint> project_paint_mesh_stroke(const std::filesystem::path&, const AssetCatalog&,
                                                  const PaintMeshSelection&,
                                                  std::span<const PaintMeshPoint>,
                                                  const PaintTextureValue&, std::stop_token = {});
} // namespace forge
