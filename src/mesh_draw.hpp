#pragma once
#include "decal_material.hpp"
#include "environment_gpu.hpp"
#include "material_shader.hpp"
#include "mesh_draw_profile.hpp"
#include "mesh_vertex_fetch.hpp"
#include "presentation_diligent.hpp"
#include "render_bounds.hpp"
#include "shadow_lighting.hpp"
#include "sprite_draw_values.hpp"
#include "transmission_background.hpp"
#include <forge/material_resource.hpp>
#include <forge/render_components.hpp>
#include <forge/render_view.hpp>
#include <forge/scene_render_settings.hpp>
namespace forge {
// Diligent prepared draw. The caller owns scene extraction, typed resource
// leases, sorting, target/pass composition and publication of complete candidates.
// No source parser, entity registry or second material interpretation lives here.
class MeshDraw {
  public:
    static constexpr unsigned instance_limit = 64;
    struct Instance {
        AffineTransform world;
        std::optional<std::array<float, 3>> legacy_tint;
        const SpriteDrawValues* sprite = nullptr; // Borrowed only until draw returns.
        const AffineTransform* previous_world = nullptr;
    };
    struct Motion {
        const CameraView* previous_view = nullptr;
        const AffineTransform* previous_world = nullptr;
        std::span<const float> previous_morph_weights;
        const SkinPose* previous_skin = nullptr;
        std::array<float, 2> jitter{}, previous_jitter{};
        bool valid = false;
    };
    bool supports_instances() const { return bool(instances_); }
    struct Decal {
        struct Texture {
            Diligent::ITextureView* view{};
            Diligent::ISampler* sampler{};
        };
        const DecalView* projection{};
        const DecalMaterial* material{};
        std::array<Texture, decal_channel_count> textures{};
        unsigned channels = decal_all_channels;
    };
    unsigned decal_capacity() const { return decal_capacity_; }
    bool accepts_decals(std::span<const Decal>) const;
    // Drop every winding SRB reference before a scene releases old GPU leases.
    void clear_decals(Diligent::IDeviceContext*);
    using Textures = std::map<std::string, Diligent::RefCntAutoPtr<Diligent::ITextureView>>;
    MeshDraw(DiligentPresentation&, Diligent::IDeviceContext*, const GpuMeshPart&,
             const MaterialData&, const Textures&, Diligent::TEXTURE_FORMAT color_format,
             Diligent::TEXTURE_FORMAT depth_format, bool enable_skin = true,
             Diligent::SHADER_COMPILER compiler = Diligent::SHADER_COMPILER_DEFAULT,
             Diligent::SHADER_OPTIMIZATION_LEVEL optimization =
                 Diligent::SHADER_OPTIMIZATION_LEVEL_DEFAULT,
             const MaterialShaderSnapshot* surface = nullptr,
             MeshDrawProfile = MeshDrawProfile::Mesh, bool area_lights = false);
    void bind_environment(const GpuEnvironment*);
    void bind_shadows(const ShadowLighting*);
    void bind_transmission(const TransmissionLighting*);
    void bind_sprite_mip(Diligent::ITextureView*);
    // Detached storage preparation before adopting new values. No SRB changes
    // or object-value publication. Returns whether this pass consumes a value.
    bool prepare_object_material_values(std::span<const MaterialParameterOverride>,
                                        std::string_view slot);
    void draw(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
              std::span<const LightView>, const EnvironmentLighting* = nullptr,
              const std::array<float, 3>* legacy_tint = nullptr,
              const ShadowLighting* shadows = nullptr, std::span<const int> shadow_slots = {},
              const TransmissionLighting* transmission = nullptr,
              std::span<const float> morph_weights = {}, const SkinPose* skin = nullptr,
              std::span<const Instance> instances = {},
              std::span<const MaterialParameterOverride> object_parameters = {},
              std::string_view material_slot = {}, const SpriteDrawValues* sprite = nullptr,
              const SceneDistanceFog* fog = nullptr, const Motion* motion = nullptr,
              std::span<const Decal> decals = {}, const RenderBounds* decal_bounds = nullptr);

  private:
    unsigned instance_rows() const;
    const MaterialParameterUniform* material_parameter_binding(std::string_view) const;
    void object_material_values(Diligent::IDeviceContext*,
                                std::span<const MaterialParameterOverride>, std::string_view slot);
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    GpuMeshPart mesh_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> pipelines_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> bindings_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> object_, lights_, environment_, shadows_,
        transmission_, fog_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> empty_shadow_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> black_environment_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> black_background_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> decals_, empty_decals_;
    DiligentPresentation::ProjectedBuffers decal_storage_, empty_decal_storage_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> white_decal_;
    Diligent::RefCntAutoPtr<Diligent::ISampler> default_decal_sampler_;
    unsigned decal_capacity_{};
    bool decals_active_{}, decal_surface_channels_{}, decal_clustered_{};
    struct DecalBindings {
        std::array<Diligent::IDeviceObject*, 8> textures{}, samplers{};
        std::array<std::array<float, decal_channel_count>, 128> slots{};
    };
    std::optional<DecalBindings> decal_bindings(std::span<const Decal>) const;
    void bind_decal_storage(const DiligentPresentation::ProjectedBuffers&);
    bool shadow_pass_{};
    bool fog_supported_ = true;
    bool temporal_{};
    bool indirect_{};
    bool reflection_{};
    bool normal_{};
    bool weighted_{};
    bool area_lights_{};
    bool sprite_{}, sprite_trim_allowed_{}, sprite_mips_{};
    bool lights_active_{};
    float volume_thickness_{};
    Diligent::RefCntAutoPtr<Diligent::IBuffer> morph_weights_, skin_, instances_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> temporal_object_, previous_morph_weights_,
        previous_skin_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> material_immutable_, material_dynamic_;
    std::vector<std::byte> material_defaults_;
    std::vector<MaterialParameterUniform> material_parameters_;
    bool material_surface_{}, material_dynamic_bound_{};
};
} // namespace forge
