#pragma once
#include "environment_gpu.hpp"
#include "material_shader.hpp"
#include "mesh_vertex_fetch.hpp"
#include "presentation_diligent.hpp"
#include <forge/render_view.hpp>
namespace forge {
// Backend-private prepared draw. The caller owns scene extraction, typed resource
// leases, sorting, target/pass composition and publication of complete candidates.
// No source parser, entity registry or second material interpretation lives here.
class MeshDraw {
  public:
    using Textures = std::map<std::string, Diligent::RefCntAutoPtr<Diligent::ITextureView>>;
    MeshDraw(DiligentPresentation&, Diligent::IDeviceContext*, const GpuMeshPart&,
             const MaterialData&, const Textures&, Diligent::TEXTURE_FORMAT color_format,
             Diligent::TEXTURE_FORMAT depth_format);
    void bind_environment(const GpuEnvironment*);
    void draw(Diligent::IDeviceContext*, const AffineTransform&, const CameraView&,
              std::span<const LightView>, const EnvironmentLighting* = nullptr,
              const std::array<float, 3>* legacy_tint = nullptr);

  private:
    GpuMeshPart mesh_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> pipelines_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> bindings_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> object_, lights_, environment_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> black_environment_;
};
} // namespace forge
