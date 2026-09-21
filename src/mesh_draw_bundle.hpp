#pragma once
#include "gpu_residency.hpp"
#include "mesh_draw.hpp"
#include "model_draw_candidate.hpp"
namespace forge {
// A complete physical candidate. Construct detached, then swap the unique owner
// only on success and after verifying the caller's catalog epoch. Destroy bundles
// before residency owners; native bindings die before their GPU leases.
class MeshDrawBundle {
  public:
    MeshDrawBundle(DiligentPresentation&, const asset_detail::PreparedModelDraw&,
                   GpuResidency<MeshAsset>&, GpuResidency<TextureAsset>&,
                   Diligent::TEXTURE_FORMAT color, Diligent::TEXTURE_FORMAT depth);
    void draw(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
              std::span<const LightView>, unsigned lod = 0);
    const std::vector<std::string>& unresolved_slots() const { return unresolved_; }
    const ResourceIdentity& mesh_identity() const { return mesh_.source(); }
    std::size_t lod_count() const { return lods_.size(); }

  private:
    GpuLease<MeshAsset> mesh_;
    std::map<asset_detail::DrawTextureKey, GpuLease<TextureAsset>> textures_;
    std::vector<std::string> unresolved_;
    std::vector<std::vector<std::unique_ptr<MeshDraw>>> lods_;
};
} // namespace forge
