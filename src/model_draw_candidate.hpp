#pragma once
#include "model_render_resource.hpp"
#include <tuple>
namespace forge::asset_detail {
using DrawTextureKey = std::pair<AssetId, TextureSemantic>;
// Immutable unsaved material values in an isolated preview host. A revision lease
// uses the same resource pool machinery; this is never a catalog publication.
struct MaterialPreviewSelection {
    AssetRef<MaterialAsset> asset;
    std::string revision;
    std::uint64_t generation{};
    MaterialResourceData data;
};
struct PreparedModelDraw {
    ResourceLease<MeshAsset> mesh;
    MeshMaterialSelection selection;
    std::map<AssetId, ResourceLease<MaterialAsset>> materials;
    std::map<DrawTextureKey, ResourceLease<TextureAsset>> textures;
};
// Complete physical identity, including pool owner/generation and texture semantic.
// This is a transient cache key, never a new authored asset or entity identity.
struct PreparedModelDrawKey {
    std::vector<ResourceIdentity> resources;
    std::vector<std::tuple<std::uint32_t, std::string, AssetId>> bindings;
    std::vector<std::pair<AssetId, TextureSemantic>> textures;
    std::vector<std::string> unresolved;
    bool skinned{};
    auto operator<=>(const PreparedModelDrawKey&) const = default;
};
inline PreparedModelDrawKey prepared_model_draw_key(const PreparedModelDraw& draw, bool skinned) {
    PreparedModelDrawKey result;
    result.skinned = skinned;
    result.resources.push_back(draw.mesh.identity());
    for (const auto& [id, material] : draw.materials) {
        (void)id;
        result.resources.push_back(material.identity());
    }
    for (const auto& [key, texture] : draw.textures) {
        result.textures.push_back(key);
        result.resources.push_back(texture.identity());
    }
    for (const auto& slot : draw.selection.bindings)
        result.bindings.emplace_back(slot.physical_slot, slot.key, slot.material.id);
    result.unresolved = draw.selection.unresolved;
    return result;
}
// One detached complete CPU candidate from a copied catalog publication. The
// presentation owner retains its old GPU bundle until this candidate AND its
// complete GPU realization succeed. No live world/device or publication here.
class ModelDrawCandidate {
  public:
    ModelDrawCandidate(std::filesystem::path project, std::shared_ptr<const AssetCatalog> catalog,
                       std::uint64_t catalog_epoch, AssetRef<MeshAsset> mesh,
                       std::vector<MaterialSlotOverride> overrides, ResourcePool<MeshAsset>& meshes,
                       std::shared_ptr<const MaterialPreviewSelection> preview = {});
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
    std::shared_ptr<const MaterialPreviewSelection> preview_;
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
