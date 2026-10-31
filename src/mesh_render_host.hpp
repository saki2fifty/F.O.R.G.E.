#pragma once
#include "mesh_draw_bundle.hpp"
#include "particle_effect_resource.hpp"
#include "render_bounds.hpp"
#include "shadow_renderer.hpp"
#include "sprite_frame_mips.hpp"
#include "surface_reflections.hpp"
#include "temporal_geometry.hpp"
#include "tile_asset_resources.hpp"
#include "tile_draw_values.hpp"
#include "weighted_transparency.hpp"
#include <forge/render_scene.hpp>
namespace forge {
struct MeshResourceInspection {
    ResourceStatistics meshes, materials, textures, particles, tile_sets, tile_maps;
    GpuResidencyStats gpu_meshes, gpu_textures, environments;
    std::vector<ResourceInfo> revisions, requests;
};
// Shared by visual hosts on one device/context and project. CPU preparation stays
// on workers; adoption and native access stay on the calling presentation thread.
class MeshResourceHost {
  public:
    MeshResourceHost(DiligentPresentation&, Diligent::IDeviceContext*, std::filesystem::path,
                     bool isolated_source_preview = false);
    void preview_material(AssetRef<MaterialAsset>, MaterialResourceData);
    void preview_particle_effect(AssetRef<ParticleEffectAsset>, ParticleEffectData);
    void preview_mesh(AssetRef<MeshAsset>, MeshData);
    void preview_tile_set(AssetRef<TileSetAsset>, std::shared_ptr<const TileSetData>);
    void preview_tile_map(AssetRef<TileMapAsset>, std::shared_ptr<const TileMapData>);
    void catalog(std::shared_ptr<const AssetCatalog>);
    std::shared_ptr<const AssetCatalog> catalog() const {
        check_thread();
        return catalog_;
    }
    void pump();
    void submit();
    MeshResourceInspection inspect() const;
    // Read-only editor consumers share the host's existing published Map/Set pools.
    ResourceTicket request_tile_map_values(AssetRef<TileMapAsset>);
    ResourceLease<TileMapAsset> tile_map_values(const ResourceTicket&);
    ResourceTicket request_tile_set_values(AssetRef<TileSetAsset>);
    ResourceLease<TileSetAsset> tile_set_values(const ResourceTicket&);

  private:
    friend class MeshSceneRenderer;
    void check_thread() const;
    const std::thread::id thread_ = std::this_thread::get_id();
    DiligentPresentation& presentation_;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context_;
    std::filesystem::path project_;
    std::shared_ptr<const AssetCatalog> catalog_;
    std::uint64_t epoch_ = 0;
    const bool isolated_source_preview_;
    std::shared_ptr<const asset_detail::MaterialPreviewSelection> material_preview_;
    std::shared_ptr<const asset_detail::MeshPreviewSelection> mesh_preview_;
    ResourcePool<MeshAsset> meshes_;
    ResourcePool<MaterialAsset> materials_;
    const ResourcePoolLimits texture_limits_{};
    ResourcePool<TextureAsset> textures_{texture_limits_};
    struct SpriteMipIdentity {
        std::string variant, revision;
    };
    static SpriteMipIdentity sprite_mip_identity(const ResourceIdentity&, const SpriteFrameMipKey&);
    ResourceTicket request_sprite_mip(const ResourceLease<TextureAsset>&, const SpriteFrameMipKey&,
                                      std::uint64_t byte_limit);
    // Weak, copied worker inputs only. Authoritative pixels stay in textures_.
    // Close/unload may revoke an owner lease while these independent jobs finish.
    std::map<std::pair<ResourceIdentity, SpriteFrameMipKey>,
             std::weak_ptr<const SpriteFrameMipInput>>
        sprite_mip_inputs_;
    ResourcePool<ParticleEffectAsset> particles_{{1, 64, 256, 128 * 1024}};
    std::map<AssetId, std::shared_ptr<const ParticleEffectData>> particle_previews_;
    ResourcePool<TileSetAsset> tile_sets_{{1, 64, 256, 16 * 1024 * 1024}};
    ResourcePool<TileMapAsset> tile_maps_{{1, 64, 256, 64 * 1024 * 1024}};
    std::map<AssetId, std::shared_ptr<const TileSetData>> tile_set_previews_;
    std::map<AssetId, std::shared_ptr<const TileMapData>> tile_map_previews_;
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
                               std::size_t draw_part_limit = 4096,
                               std::uint64_t material_budget = 16 * 1024 * 1024);
    bool update(const RenderScene&);
    // Capture the last completed rendered frame, after all its camera views.
    // Authored updates may occur between frames; they must not advance history.
    void commit_temporal_frame(bool enabled);
    // Borrowed view-local pass routing. The queue, culling, sorting, batching and
    // resources remain owned here; only eligible Mesh parts use the jittered view.
    struct TemporalPass {
        CameraView current, previous;
        std::array<float, 2> jitter{}, previous_jitter{};
        bool valid = false, applied = false, successful = false;
        bool motion_output = true, indirect_output = false, reflection_output = false;
        const TemporalMeshParts* previous_parts = nullptr;
        TemporalMeshParts* current_parts = nullptr;
        std::function<void()> begin;
        std::function<void(bool complete_geometry)> resolve;
        bool normal_only_output = false;
    };
    // Optional view-local spatial work after the eligible Mesh prefix. This
    // renderer still owns admission, queue order and restoring camera targets.
    struct PostMeshPass {
        std::function<void()> prepare, resolve;
        bool applied = false;
    };
    void shadows(const RenderScene&, const CameraView&, std::uint32_t layers);
    void draw(const RenderScene&, const CameraView&, std::uint32_t layers,
              Diligent::ITexture* color = nullptr, Diligent::ITextureView* depth = nullptr,
              TemporalPass* temporal = nullptr, PostMeshPass* post_mesh = nullptr);
    std::uint64_t resource_epoch() const { return host_->epoch_; }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    std::size_t omitted_diagnostics() const { return omitted_; }
    // Asynchronous resource work only. A loaded candidate rejected by pose or
    // capacity validation has diagnostics, not pending work; scene changes can retry it.
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
    std::uint64_t material_payload_bytes() const;
    EntityId pick(const RenderScene&, const CameraView&, double x, double y,
                  std::uint32_t layers = UINT32_MAX, double point_line_radius = 5) const;
    std::optional<std::pair<EntityId, double>> pick_depth(const RenderScene&, const CameraView&,
                                                          double x, double y,
                                                          std::uint32_t layers = UINT32_MAX,
                                                          double point_line_radius = 5) const;
    const EnvironmentLease& environment() const { return environment_.ready; }
    std::span<const SurfaceReflections::Probe> reflection_probes() const {
        return reflection_probes_;
    }
    std::vector<SurfaceReflections::Probe> reflection_probes(const CameraView&);

  private:
    enum class DrawScope { Mesh, Particle, Sprite, TileMap };
    static DrawScope scope(const RenderMesh& mesh) {
        return mesh.particle_source ? DrawScope::Particle : DrawScope::Mesh;
    }
    struct SpriteMipsPending {};
    struct SpriteMipBank {
        struct Frame {
            ResourceTicket ticket;
            ResourceLease<TextureAsset> pixels;
            GpuLease<TextureAsset> image;
        };
        ResourceIdentity source;
        SpriteRenderer layout;
        std::shared_ptr<const TileSetData> definitions;
        SpriteFrameMipPlan plan;
        std::map<SpriteFrameMipKey, Frame> frames;
        bool complete = false;
        std::uint64_t metadata_bytes() const;
    };
    struct SpriteValues {
        SpriteRenderer requested, admitted;
        SpriteDrawValues draw;
        // Derived only from the successfully admitted immutable selection.
        std::uint32_t texture_width = 0, texture_height = 0;
        bool checked = false, ready = false;
        bool mip_pending = false;
        std::shared_ptr<SpriteMipBank> mip_candidate, mips;
        std::uint64_t metadata_bytes() const;
    };
    struct Entry {
        AssetRef<MeshAsset> mesh;
        std::vector<MaterialSlotOverride> overrides;
        AssetRef<MaterialVariantAsset> variant;
        std::uint64_t epoch = 0;
        std::unique_ptr<SpriteValues> sprite;
        std::unique_ptr<asset_detail::ModelDrawCandidate> candidate;
        std::shared_ptr<MeshDrawBundle> ready;
        std::shared_ptr<const MeshInstancePose> pose, previous_pose;
        std::optional<ResourceIdentity> previous_mesh;
        std::shared_ptr<const MeshInstancePose> stage_pose(MeshInstancePose) const;
        std::shared_ptr<const MeshPoseGeometry> candidate_geometry;
        std::vector<float> thresholds;
        std::vector<MaterialParameterOverride> requested_parameters, parameters;
        bool parameters_checked = false, resource_current = false;
        std::string parameter_error;
        std::string error, pose_error;
        std::optional<bool> failed_skin_mode;
        bool fallback = false;
        std::uint64_t pose_bytes() const;
        std::uint64_t material_bytes() const;
    };
    std::shared_ptr<MeshDrawBundle> prepare_bundle(const asset_detail::PreparedModelDraw&, bool,
                                                   std::shared_ptr<const MeshPoseGeometry>,
                                                   MeshDrawProfile = MeshDrawProfile::Mesh);
    std::shared_ptr<SpriteMipBank> prepare_sprite_mips(Entry&,
                                                       const asset_detail::PreparedModelDraw&,
                                                       EntityId, DrawScope, std::uint64_t image);
    std::map<asset_detail::PreparedModelDrawKey, std::weak_ptr<MeshDrawBundle>> bundles_;
    struct EnvironmentSelection {
        AssetRef<TextureAsset> source;
        std::uint64_t epoch = UINT64_MAX;
        std::optional<ResourceTicket> candidate;
        EnvironmentLease ready;
        std::string error;
    };
    bool resolve_environment(EnvironmentSelection&, AssetRef<TextureAsset>);
    bool update_environment(const RenderScene&);
    bool update_reflection_probes(const RenderScene&);
    bool update_decals(const RenderScene&);
    bool update_light_images(const RenderScene&);
    void clear_decal_bindings();
    void report(Diagnostic);
    void report(EntityId, const std::string&, DrawScope = DrawScope::Mesh);
    std::shared_ptr<MeshResourceHost> host_;
    std::unique_ptr<ShadowRenderer> shadows_;
    std::unique_ptr<TransmissionBackground> transmission_;
    std::unique_ptr<WeightedTransparency> weighted_;
    Diligent::TEXTURE_FORMAT color_;
    const std::uint64_t pose_budget_;
    const std::size_t draw_part_limit_;
    const std::uint64_t material_budget_;
    AssetId scene_;
    EnvironmentSelection environment_;
    struct ProbeEntry {
        ReflectionProbe value;
        AffineTransform world;
        ReflectionProbeVolume volume;
        EnvironmentSelection environment;
    };
    std::map<EntityId, ProbeEntry> probe_entries_;
    std::vector<SurfaceReflections::Probe> reflection_probes_;
    struct ProjectedTextureResource {
        ResourceLease<TextureAsset> source;
        GpuLease<TextureAsset> gpu;
        Diligent::RefCntAutoPtr<Diligent::ISampler> sampler;
    };
    struct DecalResources {
        ResourceLease<MaterialAsset> material;
        DecalMaterial values;
        std::array<ProjectedTextureResource, decal_channel_count> textures;
        unsigned channels = 0;
    };
    struct DecalEntry {
        DecalView projector;
        AssetRef<MaterialAsset> requested;
        std::uint64_t epoch = UINT64_MAX;
        unsigned channels = 0;
        std::optional<ResourceTicket> material_ticket;
        std::array<std::optional<ResourceTicket>, decal_channel_count> texture_tickets;
        std::optional<DecalResources> candidate, ready;
        std::string error;
    };
    std::map<EntityId, DecalEntry> decal_entries_;
    std::vector<EntityId> decal_order_;
    struct LightImageEntry {
        AssetRef<TextureAsset> requested;
        TextureDimension dimension = TextureDimension::D2;
        std::uint64_t epoch = UINT64_MAX;
        std::optional<ResourceTicket> ticket;
        std::optional<ProjectedTextureResource> ready;
        std::string error;
    };
    std::map<EntityId, LightImageEntry> light_image_entries_;
    struct DrawOwner {
        EntityId first;
        DrawScope second;
        std::uint64_t image = 0; // Scoped Tile Set image key, never an entity UUID.
        auto operator<=>(const DrawOwner&) const = default;
    };
    std::map<DrawOwner, Entry> entries_;
    // Detached requested proxies. Entry owns admitted values/resources/poses.
    std::vector<RenderMesh> sprites_;
    struct TileImageInput {
        RenderMesh mesh;
        SpriteRenderer sprite;
        std::uint64_t image = 0;
    };
    struct TileImageDraw {
        std::shared_ptr<SpriteMipBank> mips;
        std::shared_ptr<MeshDrawBundle> bundle;
        RenderMesh mesh;
        std::uint32_t width = 0, height = 0;
    };
    struct TileDrawSource {
        struct Load {
            AssetRef<TileMapAsset> asset;
            std::uint64_t epoch = UINT64_MAX;
            ResourceTicket map_ticket, set_ticket;
            ResourceLease<TileMapAsset> map_lease;
            ResourceLease<TileSetAsset> set_lease;
            std::shared_ptr<const TileMapData> cells;
            std::shared_ptr<const TileSetData> definitions;
            std::string error;
        } load;
        RenderTileMap requested;
        std::shared_ptr<const TileMapData> cells;
        std::shared_ptr<const TileSetData> definitions;
        std::map<std::uint64_t, TileImageDraw> images;
        std::vector<std::uint64_t> used_images;
        std::map<std::uint64_t, RenderBounds> requested_bounds, bounds;
        std::size_t requested_visible_cells = 0, visible_cells = 0;
        struct Values {
            SpriteDrawValues sprite;
            const GpuLease<TextureAsset>* mip = nullptr; // Retained by images' complete banks.
        };
        std::map<std::pair<std::uint64_t, std::uint32_t>, Values> values;
        std::array<float, 4> color{};
        bool pending = false, visible_this_view = false;
        bool rejected = false;
    };
    std::vector<TileImageInput> tile_images_;
    std::map<EntityId, TileDrawSource> tile_sources_;
    std::vector<AffineTransform> tile_worlds_;
    std::vector<TileImageInput> prepare_tile_sources(const RenderScene&);
    RenderTileMap resolve_tile_assets(TileDrawSource&, const RenderTileMap&);
    void adopt_tile_images();
    std::uint64_t tile_material_bytes() const;
    struct ParticleSource {
        AssetRef<ParticleEffectAsset> effect;
        std::uint64_t epoch = 0;
        ResourceTicket effect_ticket, mesh_ticket;
        ResourceLease<ParticleEffectAsset> effect_lease;
        ResourceLease<MeshAsset> mesh_lease;
        std::shared_ptr<const ParticleEffectData> preview;
        std::string error;
        const ParticleEffectData* data() const {
            return preview ? preview.get() : effect_lease ? &effect_lease.get() : nullptr;
        }
    };
    std::vector<RenderMesh> prepare_particle_sources(const RenderScene&);
    void update_particle_instances(const RenderScene&);
    std::map<EntityId, ParticleSource> particle_sources_;
    std::vector<std::pair<RenderMesh, MeshInstancePose>> particle_instances_;
    std::vector<std::pair<EntityId, RenderBounds>> particle_bounds_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t omitted_ = 0;
    DrawStats draw_stats_;
};
} // namespace forge
