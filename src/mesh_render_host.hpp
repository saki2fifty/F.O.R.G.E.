#pragma once
#include "mesh_draw_bundle.hpp"
#include "render_bounds.hpp"
#include "shadow_renderer.hpp"
#include <forge/render_scene.hpp>
namespace forge {
struct MeshResourceInspection {
    ResourceStatistics meshes, materials, textures;
    GpuResidencyStats gpu_meshes, gpu_textures, environments;
    std::vector<ResourceInfo> revisions, requests;
};
// Shared by visual hosts on one device/context and project. CPU preparation stays
// on workers; adoption and native access stay on the calling presentation thread.
class MeshResourceHost {
  public:
    MeshResourceHost(DiligentPresentation&, Diligent::IDeviceContext*, std::filesystem::path,
                     bool isolated_material_preview = false);
    void preview_material(AssetRef<MaterialAsset>, MaterialResourceData);
    void catalog(std::shared_ptr<const AssetCatalog>);
    std::shared_ptr<const AssetCatalog> catalog() const {
        check_thread();
        return catalog_;
    }
    void pump();
    void submit();
    MeshResourceInspection inspect() const;

  private:
    friend class MeshSceneRenderer;
    void check_thread() const;
    const std::thread::id thread_ = std::this_thread::get_id();
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::filesystem::path project_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::uint64_t epoch_ = 0;
    const bool isolated_material_preview_;
    std::shared_ptr<const asset_detail::MaterialPreviewSelection> material_preview_;
    ResourcePool<MeshAsset> meshes_;
    ResourcePool<MaterialAsset> materials_;
    ResourcePool<TextureAsset> textures_;
    EnvironmentResidency environments_;
    GpuResidency<MeshAsset> gpu_meshes_;
    GpuResidency<TextureAsset> gpu_textures_;
};
// One visual scene's derived revision selections. Flecs/extraction owns entities,
// transforms and values; this cache only retains complete draw-resource bundles.
class MeshSceneRenderer {
  public:
    explicit MeshSceneRenderer(std::shared_ptr<MeshResourceHost>,
                               Diligent::TEXTURE_FORMAT color = Diligent::TEX_FORMAT_RGBA8_UNORM,
                               std::uint64_t pose_budget = mesh_pose_payload_limit,
                               std::size_t draw_part_limit = 4096);
    bool update(const RenderScene&);
    void shadows(const RenderScene&, const CameraView&, std::uint32_t layers);
    void draw(const RenderScene&, const CameraView&, std::uint32_t layers,
              Diligent::ITexture* color = nullptr, Diligent::ITextureView* depth = nullptr);
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    std::size_t omitted_diagnostics() const { return omitted_; }
    bool pending() const;
    struct DrawStats {
        std::size_t calls{}, instances{}, batched_calls{};
    };
    DrawStats draw_stats() const { return draw_stats_; }
    std::size_t bundle_count() const;
    std::optional<RenderBounds> bounds() const;
    // Current rendered poses only; an unready member rejects partial framing.
    // Empty selection fits visible scene meshes, explicit selection also permits hidden ones.
    std::optional<RenderBounds> bounds(const RenderScene&, const std::set<EntityId>&,
                                       Double3 camera_origin) const;

    std::uint64_t pose_payload_bytes() const;
    EntityId pick(const RenderScene&, const CameraView&, double x, double y,
                  std::uint32_t layers = UINT32_MAX, double point_line_radius = 5) const;
    const EnvironmentLease& environment() const { return environment_ready_; }

  private:
    struct Entry {
        AssetRef<MeshAsset> mesh;
        std::vector<MaterialSlotOverride> overrides;
        std::uint64_t epoch = 0;
        std::unique_ptr<asset_detail::ModelDrawCandidate> candidate;
        std::shared_ptr<MeshDrawBundle> ready;
        MeshInstancePose pose;
        std::shared_ptr<const MeshPoseGeometry> candidate_geometry;
        std::vector<float> thresholds;
        std::string error, pose_error;
        std::optional<bool> failed_skin_mode;
        std::uint64_t pose_bytes() const;
    };
    std::shared_ptr<MeshDrawBundle> prepare_bundle(const asset_detail::PreparedModelDraw&, bool,
                                                   std::shared_ptr<const MeshPoseGeometry>);
    std::map<asset_detail::PreparedModelDrawKey, std::weak_ptr<MeshDrawBundle>> bundles_;
    bool update_environment(const RenderScene&);
    void report(EntityId, const std::string&);
    std::shared_ptr<MeshResourceHost> host_;
    std::unique_ptr<ShadowRenderer> shadows_;
    std::unique_ptr<TransmissionBackground> transmission_;
    Diligent::TEXTURE_FORMAT color_;
    const std::uint64_t pose_budget_;
    const std::size_t draw_part_limit_;
    AssetId scene_;
    AssetRef<TextureAsset> environment_source_;
    std::uint64_t environment_epoch_ = 0;
    std::optional<ResourceTicket> environment_candidate_;
    EnvironmentLease environment_ready_;
    std::string environment_error_;
    std::map<EntityId, Entry> entries_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t omitted_ = 0;
    DrawStats draw_stats_;
};
} // namespace forge
