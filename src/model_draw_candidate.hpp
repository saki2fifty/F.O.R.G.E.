#pragma once
#include "model_render_resource.hpp"
namespace forge::asset_detail {
using DrawTextureKey = std::pair<AssetId, TextureSemantic>;
struct PreparedModelDraw {
    ResourceLease<MeshAsset> mesh;
    MeshMaterialSelection selection;
    std::map<AssetId, ResourceLease<MaterialAsset>> materials;
    std::map<DrawTextureKey, ResourceLease<TextureAsset>> textures;
};
// One detached complete CPU candidate from a copied catalog publication. The
// presentation owner retains its old GPU bundle until this candidate AND its
// complete GPU realization succeed. No live world/device or publication here.
class ModelDrawCandidate {
  public:
    ModelDrawCandidate(std::filesystem::path project, std::shared_ptr<const AssetCatalog> catalog,
                       std::uint64_t catalog_epoch, AssetRef<MeshAsset> mesh,
                       std::vector<MaterialSlotOverride> overrides,
                       ResourcePool<MeshAsset>& meshes);
    void advance(std::uint64_t current_catalog_epoch, ResourcePool<MeshAsset>& meshes,
                 ResourcePool<MaterialAsset>& materials, ResourcePool<TextureAsset>& textures);
    void cancel(); // Only this consumer; coalesced pool requests remain usable.
    ResourceState state() const;
    const std::string& diagnostic() const;
    const PreparedModelDraw* ready() const;

  private:
    void check_thread() const;
    void fail(ResourceState state, std::string message);
    const std::thread::id thread_ = std::this_thread::get_id();
    const std::uint64_t epoch_;
    std::filesystem::path project_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::vector<MaterialSlotOverride> overrides_;
    ResourceState state_ = ResourceState::Loading;
    std::string diagnostic_;
    PreparedModelDraw prepared_;
    ResourceTicket mesh_;
    std::map<AssetId, ResourceTicket> materials_;
    std::map<DrawTextureKey, ResourceTicket> textures_;
    unsigned stage_ = 0;
};
} // namespace forge::asset_detail
