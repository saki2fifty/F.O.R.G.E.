#pragma once
#include "depth_of_field.hpp"
#include "display_resolve.hpp"
#include "environment_sky.hpp"
#include "hdr_bloom.hpp"
#include "mesh_render_host.hpp"
#include "temporal_frame_sample.hpp"
#include "temporal_view.hpp"
#include <optional>
namespace forge {
// Shared visual frame composition. No ImGui, editor camera, ECS ownership or
// authoring service; both editor views and a future standalone host can call it.
class FrameRenderer {
  public:
    explicit FrameRenderer(DiligentPresentation&, std::string profile_view = "Game");
    void resources(std::shared_ptr<MeshResourceHost>);
    // Presentation discontinuity reported by the existing preview/seek owner.
    void invalidate_temporal_history();
    Diligent::ITextureView* game(Diligent::IDeviceContext*, const nlohmann::json&, unsigned width,
                                 unsigned height, double presentation_seconds = 0,
                                 const TemporalFrameSample* sample = nullptr);
    Diligent::ITextureView* game(Diligent::IDeviceContext*, RenderScene, unsigned width,
                                 unsigned height, double presentation_seconds = 0,
                                 const TemporalFrameSample* sample = nullptr);
    Diligent::ITextureView* render(Diligent::IDeviceContext*, const RenderScene&,
                                   std::span<const PreparedCamera>, unsigned width, unsigned height,
                                   float exposure, const SceneAutoExposure* automatic = nullptr,
                                   double presentation_seconds = 0,
                                   const TemporalFrameSample* sample = nullptr);
    bool temporal_ready() const;
    bool pending() const { return meshes_ && meshes_->pending(); }
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    std::size_t omitted_diagnostics() const { return omitted_; }
    const std::vector<PreparedCamera>& cameras() const { return cameras_; }
    // Snapshot used by the last game() call, shared with optional debug overlays.
    const RenderScene* game_scene() const { return game_scene_ ? &*game_scene_ : nullptr; }
    Diligent::ITextureView* depth() const;
    Diligent::ITextureView* output() const { return display_.output(); }
    std::optional<Double3> pick_point(const RenderScene&, const CameraView&, double x,
                                      double y) const;
    std::uint64_t frames = 0;
    std::optional<RenderBounds> bounds() const {
        return meshes_ ? meshes_->bounds() : std::nullopt;
    }

  private:
    void targets(unsigned width, unsigned height);
    void clear_camera(Diligent::IDeviceContext*, const Camera&);
    void report(const Diagnostic&);
    const std::thread::id thread_ = std::this_thread::get_id();
    DiligentPresentation& presentation_;
    const std::string profile_view_;
    DisplayResolve display_;
    HdrBloom bloom_;
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
    struct ExposureView {
        EntityId entity;
        PixelViewport viewport;
        std::uint32_t projection;
        bool operator==(const ExposureView&) const = default;
    };
    std::vector<ExposureView> exposure_views_;
    AssetId exposure_scene_;
    std::array<unsigned, 2> exposure_extent_{};
    std::optional<double> exposure_time_;
    bool exposure_was_pending_ = false;
    struct CameraEffects {
        EntityId entity;
        CameraView unjittered;
        std::uint32_t layers = 0;
        std::unique_ptr<TemporalView> history;
        std::unique_ptr<DepthOfField> focus;
        bool applied = false;
    };
    std::vector<CameraEffects> camera_effects_;
    AssetId temporal_scene_;
    SceneRenderSettings temporal_settings_;
    std::optional<double> temporal_time_;
    std::uint64_t temporal_epoch_ = 0;
    std::array<unsigned, 2> temporal_extent_{};
    bool temporal_was_pending_ = false;
    std::optional<TemporalFrameSample> temporal_sample_;
};
} // namespace forge
