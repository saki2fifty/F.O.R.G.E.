#pragma once
#include "mesh_draw_bundle.hpp"
#include "render_bounds.hpp"
#include <forge/render_scene.hpp>
namespace forge {
// Shared by visual hosts on one device/context and project. CPU preparation stays
// on workers; adoption and native access stay on the calling presentation thread.
class MeshResourceHost {
  public:
    MeshResourceHost(DiligentPresentation&, Diligent::IDeviceContext*, std::filesystem::path);
    void catalog(std::shared_ptr<const AssetCatalog>);
    void pump();
    void submit();

  private:
    friend class MeshSceneRenderer;
    void check_thread() const;
    const std::thread::id thread_ = std::this_thread::get_id();
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::filesystem::path project_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::uint64_t epoch_ = 0;
    ResourcePool<MeshAsset> meshes_;
    ResourcePool<MaterialAsset> materials_;
    ResourcePool<TextureAsset> textures_;
    GpuResidency<MeshAsset> gpu_meshes_;
    GpuResidency<TextureAsset> gpu_textures_;
};
// One visual scene's derived revision selections. Flecs/extraction owns entities,
// transforms and values; this cache only retains complete draw-resource bundles.
class MeshSceneRenderer {
  public:
    explicit MeshSceneRenderer(std::shared_ptr<MeshResourceHost>,
                               Diligent::TEXTURE_FORMAT color = Diligent::TEX_FORMAT_RGBA8_UNORM);
    bool update(const RenderScene&);
    void draw(const RenderScene&, const CameraView&, std::uint32_t layers);
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    std::size_t omitted_diagnostics() const { return omitted_; }
    bool pending() const;

  private:
    struct Entry {
        AssetRef<MeshAsset> mesh;
        std::vector<MaterialSlotOverride> overrides;
        std::uint64_t epoch = 0;
        std::unique_ptr<asset_detail::ModelDrawCandidate> candidate;
        std::unique_ptr<MeshDrawBundle> ready;
        MeshBounds bounds;
        std::vector<float> thresholds;
        std::string error;
    };
    void report(EntityId, const std::string&);
    std::shared_ptr<MeshResourceHost> host_;
    Diligent::TEXTURE_FORMAT color_;
    AssetId scene_;
    std::map<EntityId, Entry> entries_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t omitted_ = 0;
};
} // namespace forge
