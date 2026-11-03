#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "camera_render_textures.hpp"
#include "depth_of_field.hpp"
#include "display_resolve.hpp"
#include "environment_sky.hpp"
#include "hdr_bloom.hpp"
#include "mesh_render_host.hpp"
#include "presentation_diligent.hpp"
#include "scene_cache.hpp"
#include "temporal_view.hpp"
#include <forge/primitive_catalog.hpp>
#include <forge/scene.hpp>
namespace forge {
class Viewport {
  public:
    std::uint64_t redraws = 0, retained = 0;
    NavigationTileContours navigation_tiles;
    explicit Viewport(DiligentPresentation& presentation, bool hdr = false);
    void preview_lighting(bool enabled) {
        if (preview_lighting_ != enabled) {
            preview_lighting_ = enabled;
            frame_.reset();
        }
    }
    void exposure(float ev) {
        if (exposure_ != ev) {
            exposure_ = ev;
            frame_.reset();
        }
    }
    void resources(std::shared_ptr<MeshResourceHost> host) {
        sky_.select({});
        meshes_.reset();
        render_textures_.reset();
        host_ = std::move(host);
        meshes_ = host_ ? std::make_unique<MeshSceneRenderer>(host_, color_format_) : nullptr;
        mesh_scene_.reset();
        frame_.reset();
        temporal_.reset();
        focus_.reset();
        temporal_camera_.reset();
    }
    const MeshSceneRenderer* meshes() const { return meshes_.get(); }
    bool pending() const {
        return (meshes_ && meshes_->pending()) || (render_textures_ && render_textures_->pending());
    }
    std::vector<Diagnostic> diagnostics() const {
        auto result = meshes_ ? meshes_->diagnostics() : std::vector<Diagnostic>{};
        if (render_textures_)
            result.insert(result.end(), render_textures_->diagnostics().begin(),
                          render_textures_->diagnostics().end());
        return result;
    }
    const RenderScene* render_scene() const { return mesh_scene_ ? &*mesh_scene_ : nullptr; }
    bool frame(const Json&, const std::string& selected, EditorCamera&, float aspect);
    bool frame_selection(const Json&, const std::vector<std::string>&, EditorCamera&, float aspect);
    std::string pick(const Json&, const EditorCamera&, unsigned width, unsigned height, double x,
                     double y, double point_line_radius = 5);
    Diligent::ITextureView* render(Diligent::IDeviceContext* context, const Json& scene,
                                   unsigned width, unsigned height, const EditorCamera& camera,
                                   std::uint64_t generation, bool live, GridSettings grid = {});

  private:
    DiligentPresentation& presentation_;
    Diligent::TEXTURE_FORMAT color_format_;
    std::unique_ptr<DisplayResolve> display_;
    HdrBloom bloom_;
    float exposure_ = 0;
    bool preview_lighting_ = false;
    EnvironmentSky sky_;
    std::shared_ptr<MeshResourceHost> host_;
    std::unique_ptr<CameraRenderTextures> render_textures_;
    std::unique_ptr<MeshSceneRenderer> meshes_;
    std::optional<RenderScene> mesh_scene_;
    std::uint64_t mesh_generation_ = 0;
    std::optional<ViewportFrameKey> frame_;
    std::unique_ptr<TemporalView> temporal_;
    std::unique_ptr<DepthOfField> focus_;
    std::optional<CameraView> temporal_camera_;
    SceneRenderSettings temporal_settings_;
    AssetId temporal_scene_;
    std::uint64_t temporal_epoch_ = 0, temporal_generation_ = 0;
    bool temporal_pending_ = false, temporal_preview_lighting_ = false;
    unsigned temporal_settle_ = 0;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device_;
    Diligent::RefCntAutoPtr<Diligent::ITexture> color_, depth_;
    std::array<Diligent::RefCntAutoPtr<Diligent::IPipelineState>, 3> pipelines_;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> grid_pipeline_;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constants_, vertices_, grid_constants_;
    std::array<unsigned, primitive_count> starts_{}, counts_{};
    std::array<Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>, 3> resources_;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> grid_resources_;
};
} // namespace forge
