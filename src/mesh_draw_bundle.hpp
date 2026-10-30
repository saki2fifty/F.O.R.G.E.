#pragma once
#include "gpu_residency.hpp"
#include "mesh_draw.hpp"
#include "model_draw_candidate.hpp"
#include "model_instance_pose.hpp"
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
        bool transmission{};
        std::size_t binding{};
        bool depth_write = true;
    };
    MeshDrawBundle(DiligentPresentation&, Diligent::IDeviceContext*,
                   const asset_detail::PreparedModelDraw&, GpuResidency<MeshAsset>&,
                   GpuResidency<TextureAsset>&, Diligent::TEXTURE_FORMAT color,
                   Diligent::TEXTURE_FORMAT depth, bool skinned = false,
                   std::shared_ptr<const MeshPoseGeometry> geometry = {},
                   MeshDrawProfile profile = MeshDrawProfile::Mesh);
    void environment(const EnvironmentLease&);
    void shadows(const ShadowLighting*);
    void transmission(const TransmissionLighting*);
    void clear_decals(Diligent::IDeviceContext*);
    unsigned decal_capacity(unsigned lod, unsigned part) const {
        return lods_.at(lod).at(part)->decal_capacity();
    }
    void prepare_material_parameters(std::span<const MaterialParameterOverride>);
    // Specialize the same admitted part before a temporal view changes targets.
    // Failure leaves the ordinary color and shadow draws available.
    void prepare_temporal(Diligent::IDeviceContext*, unsigned lod, unsigned part,
                          std::span<const MaterialParameterOverride> = {});
    bool temporal_prepared(unsigned lod, unsigned part) const {
        return surface_prepared(lod, part, true, false);
    }
    void prepare_surface(Diligent::IDeviceContext*, unsigned lod, unsigned part, bool motion,
                         bool indirect, std::span<const MaterialParameterOverride> = {},
                         bool reflection = false, bool normal = false, bool area_lights = false);
    bool surface_prepared(unsigned lod, unsigned part, bool motion, bool indirect,
                          bool reflection = false, bool normal = false,
                          bool area_lights = false) const {
        const auto& variants =
            surface_lods_.at(surface_index(motion, indirect, reflection, normal, area_lights));
        if (variants.empty()) {
            (void)info_.at(lod).at(part);
            return false;
        }
        return bool(variants.at(lod).at(part));
    }
    std::size_t draw_part_count() const;
    void draw_shadow(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
                     unsigned lod = 0, const MeshInstancePose* pose = nullptr,
                     std::span<const MaterialParameterOverride> parameters = {});
    void draw(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
              std::span<const LightView>, const EnvironmentLighting* = nullptr, unsigned lod = 0);
    void draw_part(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
                   std::span<const LightView>, unsigned lod, unsigned part,
                   const EnvironmentLighting* = nullptr,
                   const std::array<float, 3>* legacy_tint = nullptr,
                   const ShadowLighting* shadows = nullptr, std::span<const int> shadow_slots = {},
                   const TransmissionLighting* transmission = nullptr,
                   const MeshInstancePose* pose = nullptr,
                   std::span<const MeshDraw::Instance> instances = {},
                   std::span<const MaterialParameterOverride> parameters = {},
                   const SpriteDrawValues* sprite = nullptr, const SceneDistanceFog* fog = nullptr,
                   const GpuLease<TextureAsset>* sprite_mip = nullptr,
                   const MeshDraw::Motion* motion = nullptr, bool indirect = false,
                   bool reflection = false, bool normal = false,
                   std::span<const MeshDraw::Decal> decals = {});
    std::string_view slot(unsigned lod, unsigned part) const {
        return prepared_.selection.bindings.at(info_.at(lod).at(part).binding).key;
    }
    bool supports_instances(unsigned lod, unsigned part) const {
        return lods_.at(lod).at(part)->supports_instances();
    }
    std::span<const PartInfo> parts(unsigned lod) const { return info_.at(lod); }
    const std::vector<std::string>& unresolved_slots() const { return unresolved_; }
    const ResourceIdentity& mesh_identity() const { return mesh_.source(); }
    const asset_detail::PreparedModelDraw& prepared() const { return prepared_; }
    const MeshPoseGeometry& geometry() const { return *geometry_; }
    std::shared_ptr<const MeshPoseGeometry> geometry_owner() const { return geometry_; }
    bool skinned() const { return skinned_; }
    MeshDrawProfile profile() const { return profile_; }
    std::size_t lod_count() const { return lods_.size(); }

  private:
    static std::size_t surface_index(bool motion, bool indirect, bool reflection = false,
                                     bool normal = false, bool area_lights = false) {
        if (normal) {
            if (!indirect || reflection)
                throw std::invalid_argument("Normal-only specialization requires indirect output "
                                            "without reflection outputs");
            return (motion ? 6 : 5) + (area_lights ? 8 : 0);
        }
        if (reflection) {
            if (!indirect)
                throw std::invalid_argument("Reflection specialization requires indirect output");
            return (motion ? 4 : 3) + (area_lights ? 8 : 0);
        }
        if (!motion && !indirect) {
            if (area_lights)
                return 7;
            throw std::invalid_argument("Surface specialization needs motion or indirect output");
        }
        return (indirect ? (motion ? 2 : 1) : 0) + (area_lights ? 8 : 0);
    }
    std::unique_ptr<MeshDraw> prepare_draw(Diligent::IDeviceContext*, unsigned lod, unsigned part,
                                           Diligent::TEXTURE_FORMAT, MeshDrawProfile,
                                           bool area_lights = false);
    DiligentPresentation& presentation_;
    Diligent::TEXTURE_FORMAT color_, depth_;
    // CPU revision leases survive every derived pose and native binding.
    asset_detail::PreparedModelDraw prepared_;
    std::shared_ptr<const MeshPoseGeometry> geometry_;
    bool skinned_ = false;
    MeshDrawProfile profile_ = MeshDrawProfile::Mesh;
    EnvironmentLease environment_;
    GpuLease<MeshAsset> mesh_;
    std::map<asset_detail::DrawTextureKey, GpuLease<TextureAsset>> textures_;
    GpuLease<TextureAsset> sprite_mip_; // Lives longer than SRBs retaining its native view.
    std::vector<std::string> unresolved_;
    std::vector<std::vector<PartInfo>> info_;
    std::vector<std::vector<std::unique_ptr<MeshDraw>>> lods_;
    std::vector<std::vector<std::unique_ptr<MeshDraw>>> shadow_lods_;
    // Existing seven MRT variants, ordinary area color, then seven area MRT
    // variants. Area layouts and native resources allocate on first use only.
    std::array<std::vector<std::vector<std::unique_ptr<MeshDraw>>>, 15> surface_lods_;
};
} // namespace forge
