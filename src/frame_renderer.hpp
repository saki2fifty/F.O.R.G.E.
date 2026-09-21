#pragma once
#include "display_resolve.hpp"
#include "environment_sky.hpp"
#include "mesh_render_host.hpp"
namespace forge {
// Shared visual frame composition. No ImGui, editor camera, ECS ownership or
// authoring service; both editor views and a future standalone host can call it.
class FrameRenderer {
  public:
    explicit FrameRenderer(DiligentPresentation&);
    void resources(std::shared_ptr<MeshResourceHost>);
    Diligent::ITextureView* game(Diligent::IDeviceContext*, const nlohmann::json&, unsigned width,
                                 unsigned height);
    Diligent::ITextureView* render(Diligent::IDeviceContext*, const RenderScene&,
                                   std::span<const PreparedCamera>, unsigned width, unsigned height,
                                   float exposure);
    bool pending() const { return meshes_ && meshes_->pending(); }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    std::size_t omitted_diagnostics() const { return omitted_; }
    const std::vector<PreparedCamera>& cameras() const { return cameras_; }
    // Snapshot used by the last game() call, shared with optional debug overlays.
    const RenderScene* game_scene() const { return game_scene_ ? &*game_scene_ : nullptr; }
    Diligent::ITextureView* depth() const;
    Diligent::ITextureView* output() const { return display_.output(); }
    std::uint64_t frames = 0;

  private:
    void targets(unsigned width, unsigned height);
    void clear_camera(Diligent::IDeviceContext*, const Camera&);
    void report(const Diagnostic&);
    const std::thread::id thread_ = std::this_thread::get_id();
    DiligentPresentation& presentation_;
    DisplayResolve display_;
    EnvironmentSky sky_;
    std::unique_ptr<MeshSceneRenderer> meshes_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_, depth_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> clear_color_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> clears_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> clear_bindings_;
    std::optional<RenderScene> game_scene_;
    std::vector<PreparedCamera> cameras_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t omitted_ = 0;
};
} // namespace forge
