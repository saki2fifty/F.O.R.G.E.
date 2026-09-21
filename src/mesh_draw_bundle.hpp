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
    struct PartInfo {
        MaterialAlpha alpha;
        AssetId material;
        MeshBounds bounds;
    };
    MeshDrawBundle(DiligentPresentation&, Diligent::IDeviceContext*,
                   const asset_detail::PreparedModelDraw&, GpuResidency<MeshAsset>&,
                   GpuResidency<TextureAsset>&, Diligent::TEXTURE_FORMAT color,
                   Diligent::TEXTURE_FORMAT depth);
    void environment(const EnvironmentLease&);
    void shadows(const ShadowLighting*);
    void draw_shadow(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
                     unsigned lod = 0);
    void draw(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
              std::span<const LightView>, const EnvironmentLighting* = nullptr, unsigned lod = 0);
    void draw_part(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
                   std::span<const LightView>, unsigned lod, unsigned part,
                   const EnvironmentLighting* = nullptr,
                   const std::array<float, 3>* legacy_tint = nullptr,
                   const ShadowLighting* shadows = nullptr, std::span<const int> shadow_slots = {});
    std::span<const PartInfo> parts(unsigned lod) const { return info_.at(lod); }
    const std::vector<std::string>& unresolved_slots() const { return unresolved_; }
    const ResourceIdentity& mesh_identity() const { return mesh_.source(); }
    std::size_t lod_count() const { return lods_.size(); }

  private:
    EnvironmentLease environment_;
    GpuLease<MeshAsset> mesh_;
    std::map<asset_detail::DrawTextureKey, GpuLease<TextureAsset>> textures_;
    std::vector<std::string> unresolved_;
    std::vector<std::vector<PartInfo>> info_;
    std::vector<std::vector<std::unique_ptr<MeshDraw>>> lods_;
    std::vector<std::vector<std::unique_ptr<MeshDraw>>> shadow_lods_;
};
} // namespace forge
