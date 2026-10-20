#pragma once
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
    };
    bool supports_instances() const { return bool(instances_); }
    using Textures = std::map<std::string, Diligent::RefCntAutoPtr<Diligent::ITextureView>>;
    MeshDraw(DiligentPresentation&, Diligent::IDeviceContext*, const GpuMeshPart&,
             const MaterialData&, const Textures&, Diligent::TEXTURE_FORMAT color_format,
             Diligent::TEXTURE_FORMAT depth_format, bool enable_skin = true,
             Diligent::SHADER_COMPILER compiler = Diligent::SHADER_COMPILER_DEFAULT,
             Diligent::SHADER_OPTIMIZATION_LEVEL optimization =
                 Diligent::SHADER_OPTIMIZATION_LEVEL_DEFAULT,
             const MaterialShaderSnapshot* surface = nullptr,
             MeshDrawProfile = MeshDrawProfile::Mesh);
    void bind_environment(const GpuEnvironment*);
    void bind_shadows(const ShadowLighting*);
    void bind_transmission(const TransmissionLighting*);
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
              std::string_view material_slot = {}, const SpriteDrawValues* sprite = nullptr);

  private:
    const MaterialParameterUniform* material_parameter_binding(std::string_view) const;
    void object_material_values(Diligent::IDeviceContext*,
                                std::span<const MaterialParameterOverride>, std::string_view slot);
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    GpuMeshPart mesh_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> pipelines_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> bindings_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> object_, lights_, environment_, shadows_,
        transmission_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> empty_shadow_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> black_environment_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> black_background_;
    bool shadow_pass_{};
    bool sprite_{};
    bool lights_active_{};
    float volume_thickness_{};
    Diligent::RefCntAutoPtr<Diligent::IBuffer> morph_weights_, skin_, instances_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> material_immutable_, material_dynamic_;
    std::vector<std::byte> material_defaults_;
    std::vector<MaterialParameterUniform> material_parameters_;
    bool material_surface_{}, material_dynamic_bound_{};
};
} // namespace forge
