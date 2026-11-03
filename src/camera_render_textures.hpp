#pragma once
#include "render_texture_gpu.hpp"
#include "render_texture_plan.hpp"
#include "temporal_frame_sample.hpp"

namespace forge {
class DiligentPresentation;
class FrameRenderer;
class MeshResourceHost;
class MeshSceneRenderer;
// View-local producer passes. CPU definitions stay in MeshResourceHost; physical
// images use the existing lease, payload budget and real fence owner.
class CameraRenderTextures {
  public:
    CameraRenderTextures(DiligentPresentation&, std::shared_ptr<MeshResourceHost>);
    ~CameraRenderTextures();
    const RenderTextureInputs& render(Diligent::IDeviceContext*, const RenderScene&,
                                      MeshSceneRenderer&, std::uint32_t consumer_layers,
                                      double seconds, const TemporalFrameSample*,
                                      const RenderTextureOverlay* overlay = nullptr);
    void invalidate_temporal_history();
    void submit();
    bool pending() const { return pending_; }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    const RenderTextureInputs& inputs() const { return inputs_; }

  private:
    struct Target {
        ResourceTicket ticket;
        ResourceLease<RenderTextureAsset> configuration;
        GpuLease<RenderTextureAsset> image;
        std::unique_ptr<FrameRenderer> renderer;
    };
    DiligentPresentation& presentation_;
    std::shared_ptr<MeshResourceHost> host_;
    // Consumers are declared later and die before this physical image owner.
    GpuResidency<RenderTextureAsset> gpu_;
    RenderTextureInputs inputs_;
    std::map<AssetId, Target> targets_;
    AssetId scene_;
    std::uint64_t epoch_ = 0;
    bool pending_ = false;
    std::vector<Diagnostic> diagnostics_;
    void report(AssetId scene, const std::string&);
};
} // namespace forge
