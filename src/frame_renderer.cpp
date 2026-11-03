#include "frame_renderer.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "auto_exposure_plan.hpp"
#include "camera_render_textures.hpp"
#include "render_projection.hpp"
namespace forge {
using namespace Diligent;
std::optional<Double3> FrameRenderer::pick_point(const RenderScene& scene, const CameraView& view,
                                                 double x, double y) const {
    if (!meshes_ || pending())
        return {};
    const auto hit = meshes_->pick_depth(scene, view, x, y, UINT32_MAX, 0);
    return hit ? unproject_render_point(view, x, y, hit->second) : std::nullopt;
}
FrameRenderer::FrameRenderer(DiligentPresentation& presentation, std::string profile_view)
    : presentation_(presentation), profile_view_(std::move(profile_view)), display_(presentation),
      bloom_(presentation), sky_(presentation, TEX_FORMAT_RGBA16_FLOAT) {
    ShaderCreateInfo ci;
    ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ci.EntryPoint = "main";
    ci.Desc.Name = "FORGE camera rectangle clear VS";
    ci.Desc.ShaderType = SHADER_TYPE_VERTEX;
    ci.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,1,1);
    })";
    RefCntAutoPtr<IShader> vs, ps;
    presentation.shader(ci, &vs);
    ci.Desc.Name = "FORGE camera rectangle clear PS";
    ci.Desc.ShaderType = SHADER_TYPE_PIXEL;
    ci.Source = R"(cbuffer ForgeCameraClear {float4 color;};
    float4 main():SV_Target0 {return color;})";
    presentation.shader(ci, &ps);
    BufferDesc buffer;
    buffer.Name = "FORGE camera background";
    buffer.Size = 16;
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    presentation.device()->CreateBuffer(buffer, nullptr, &clear_color_);
    if (!clear_color_)
        throw std::runtime_error("Camera clear buffer allocation failed");
    for (unsigned mask = 1; mask <= 4; ++mask) {
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE camera rectangle clear";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        info.GraphicsPipeline.NumRenderTargets = 1;
        info.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
        info.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
        info.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        info.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
        info.GraphicsPipeline.DepthStencilDesc.DepthEnable = (mask & 2) != 0;
        info.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = (mask & 2) != 0;
        info.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_ALWAYS;
        info.GraphicsPipeline.BlendDesc.RenderTargets[0].RenderTargetWriteMask =
            mask == 4    ? COLOR_MASK_ALPHA
            : (mask & 1) ? COLOR_MASK_ALL
                         : COLOR_MASK_NONE;
        info.pVS = vs;
        info.pPS = ps;
        presentation.graphics(info, &clears_[mask - 1]);
        clears_[mask - 1]->CreateShaderResourceBinding(&clear_bindings_[mask - 1], true);
        auto* variable = clear_bindings_[mask - 1] ? clear_bindings_[mask - 1]->GetVariableByName(
                                                         SHADER_TYPE_PIXEL, "ForgeCameraClear")
                                                   : nullptr;
        if (!variable)
            throw std::runtime_error("Camera clear binding is unavailable");
        variable->Set(clear_color_);
    }
}
FrameRenderer::~FrameRenderer() = default;
bool FrameRenderer::pending() const {
    return (meshes_ && meshes_->pending()) || (render_textures_ && render_textures_->pending());
}
void FrameRenderer::invalidate_temporal_history() {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("Frame history changed off presentation thread");
    for (auto& camera : camera_effects_)
        if (camera.history)
            camera.history->invalidate();
    if (meshes_)
        meshes_->commit_temporal_frame(false);
    if (render_textures_)
        render_textures_->invalidate_temporal_history();
    temporal_time_.reset();
    temporal_sample_.reset();
}
void FrameRenderer::resources(std::shared_ptr<MeshResourceHost> host) {
    invalidate_temporal_history();
    sky_.select({});
    camera_effects_.clear();
    meshes_.reset();
    render_textures_.reset();
    host_ = std::move(host);
    meshes_ = host_ ? std::make_unique<MeshSceneRenderer>(host_, TEX_FORMAT_RGBA16_FLOAT) : nullptr;
    exposure_time_.reset();
    exposure_views_.clear();
    exposure_was_pending_ = false;
}
void FrameRenderer::targets(unsigned width, unsigned height) {
    const auto maximum = presentation_.device()->GetAdapterInfo().Texture.MaxTexture2DDimension;
    if (!width || !height || width > maximum || height > maximum)
        throw std::runtime_error("Frame target dimensions exceed the device profile");
    if (color_ && color_->GetDesc().Width == width && color_->GetDesc().Height == height)
        return;
    TextureDesc desc;
    desc.Name = "FORGE composed HDR frame";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    presentation_.device()->CreateTexture(desc, nullptr, &color);
    desc.Name = "FORGE composed frame depth";
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags =
        BIND_DEPTH_STENCIL |
        (presentation_.device()->GetTextureFormatInfoExt(TEX_FORMAT_D32_FLOAT).BindFlags &
         BIND_SHADER_RESOURCE);
    presentation_.device()->CreateTexture(desc, nullptr, &depth);
    if (!color || !depth)
        throw std::runtime_error("Frame allocation failed; previous targets retained");
    color_ = std::move(color);
    depth_ = std::move(depth);
}
void FrameRenderer::clear_camera(IDeviceContext* context, const Camera& settings, bool alpha_only) {
    const unsigned mask =
        alpha_only ? 4 : unsigned(settings.clear_color) | (unsigned(settings.clear_depth) << 1);
    if (!mask)
        return;
    {
        MapHelper<float> data(context, clear_color_, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = settings.background_r;
        data[1] = settings.background_g;
        data[2] = settings.background_b;
        data[3] = settings.background_a;
    }
    context->SetPipelineState(clears_[mask - 1]);
    context->CommitShaderResources(clear_bindings_[mask - 1],
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs draw{3, DRAW_FLAG_VERIFY_ALL};
    context->Draw(draw);
}
void FrameRenderer::report(const Diagnostic& value) {
    if (diagnostics_.size() < 256)
        diagnostics_.push_back(value);
    else
        ++omitted_;
}
ITextureView* FrameRenderer::game(IDeviceContext* context, const nlohmann::json& source,
                                  unsigned width, unsigned height, double seconds,
                                  const TemporalFrameSample* sample,
                                  const RenderTextureOverlay* overlay) {
    return game(context, extract_render_scene(source), width, height, seconds, sample, overlay);
}
ITextureView* FrameRenderer::game(IDeviceContext* context, RenderScene scene, unsigned width,
                                  unsigned height, double seconds,
                                  const TemporalFrameSample* sample,
                                  const RenderTextureOverlay* overlay) {
    const auto cameras = prepare_game_cameras(scene, width, height);
    auto* result =
        render(context, scene, cameras.cameras, width, height, scene.settings.exposure,
               &scene.settings.auto_exposure, seconds, sample, Output::Display, nullptr, overlay);
    for (const auto& error : cameras.diagnostics)
        report(error);
    omitted_ += cameras.omitted_diagnostics;
    game_scene_ = std::move(scene);
    return result;
}
ITextureView* FrameRenderer::render(IDeviceContext* context, const RenderScene& scene,
                                    std::span<const PreparedCamera> cameras, unsigned width,
                                    unsigned height, float exposure,
                                    const SceneAutoExposure* automatic, double seconds,
                                    const TemporalFrameSample* sample, Output output,
                                    const RenderTextureInputs* render_textures,
                                    const RenderTextureOverlay* overlay) {
    if (thread_ != std::this_thread::get_id() || !context)
        throw std::runtime_error("Frame rendering requires its presentation thread and context");
    if (output != Output::Display && output != Output::LinearHdr &&
        output != Output::LinearHdrOpaque)
        throw std::invalid_argument("Unknown frame output profile");
    validate_scene_distance_fog(scene.settings.distance_fog);
    validate_scene_temporal_anti_aliasing(scene.settings.temporal_anti_aliasing);
    validate_scene_depth_of_field(scene.settings.depth_of_field);
    validate_scene_motion_blur(scene.settings.motion_blur);
    validate_scene_ambient_occlusion(scene.settings.ambient_occlusion);
    validate_scene_screen_space_reflections(scene.settings.screen_space_reflections);
    if (sample && (!sample->operation || !std::isfinite(sample->seconds) || sample->seconds < 0))
        throw std::invalid_argument(
            "Temporal frame sample requires an operation and finite nonnegative time");
    const auto sample_key = sample ? std::optional<TemporalFrameSample>(*sample) : std::nullopt;
    if (automatic)
        validate_scene_auto_exposure(*automatic);
    const bool adaptive = output == Output::Display && automatic && automatic->enabled;
    const auto exposure_timing = !adaptive ? AutoExposureTiming{}
                                 : sample  ? AutoExposureTiming{0, temporal_sample_ != sample_key}
                                           : auto_exposure_timing(exposure_time_, seconds);
    if (adaptive)
        exposure_views_.reserve(
            cameras.size()); // Stage allocation before rendering/history adoption.
    targets(width, height);
    if (cameras.data() != cameras_.data())
        cameras_.assign(cameras.begin(), cameras.end());
    diagnostics_.clear();
    omitted_ = 0;
    if (meshes_) {
        std::uint32_t consumer_layers = 0;
        for (const auto& camera : cameras)
            consumer_layers |= camera.settings.layers;
        if (render_textures) {
            meshes_->render_textures(*render_textures);
        } else if (render_textures_ ||
                   std::any_of(scene.cameras.begin(), scene.cameras.end(),
                               [](const auto& c) { return bool(c.camera.render_texture.id); })) {
            if (!render_textures_)
                render_textures_ = std::make_unique<CameraRenderTextures>(presentation_, host_);
            meshes_->render_textures(render_textures_->render(
                context, scene, *meshes_, consumer_layers, seconds, sample, overlay));
        }
        meshes_->update(scene, cameras);
        if (!render_textures && !render_textures_ &&
            !meshes_->render_texture_reads(scene, UINT32_MAX).empty()) {
            render_textures_ = std::make_unique<CameraRenderTextures>(presentation_, host_);
            meshes_->render_textures(render_textures_->render(
                context, scene, *meshes_, consumer_layers, seconds, sample, overlay));
            meshes_->update(scene, cameras);
        }
        if (!render_textures && render_textures_)
            for (const auto& error : render_textures_->diagnostics())
                report(error);
        sky_.select(meshes_->environment());
    }
    const bool pending_resources = pending();
    bool reset_exposure = exposure_timing.reset || exposure_scene_ != scene.scene ||
                          exposure_extent_ != std::array<unsigned, 2>{width, height} ||
                          exposure_views_.size() != cameras.size() || pending_resources ||
                          exposure_was_pending_ || temporal_sample_ != sample_key;
    if (adaptive && !reset_exposure)
        for (std::size_t i = 0; i < cameras.size(); ++i)
            if (exposure_views_[i] != ExposureView{cameras[i].entity, cameras[i].view.viewport,
                                                   cameras[i].settings.projection}) {
                reset_exposure = true;
                break;
            }
    const auto temporal_settings = scene.settings.temporal_anti_aliasing;
    const bool aa_enabled = temporal_settings.enabled && meshes_;
    const bool blur_enabled = meshes_ && scene.settings.motion_blur.enabled &&
                              scene.settings.motion_blur.shutter != 0 &&
                              scene.settings.motion_blur.max_pixels != 0;
    const auto& occlusion = scene.settings.ambient_occlusion;
    const bool occlusion_enabled =
        meshes_ && occlusion.enabled && occlusion.radius != 0 && occlusion.strength != 0;
    const auto& reflections = scene.settings.screen_space_reflections;
    const bool reflections_enabled = meshes_ && reflections.enabled &&
                                     reflections.max_distance != 0 && reflections.strength != 0 &&
                                     reflections.max_roughness != 0;
    const bool local_reflections = meshes_ && meshes_->has_reflection_probes();
    const bool temporal_enabled =
        aa_enabled || blur_enabled || occlusion_enabled || reflections_enabled || local_reflections;
    const auto transition = temporal_sample_transition(temporal_sample_, sample_key);
    const TemporalView::Options temporal_options{bool(aa_enabled),  scene.settings.motion_blur,
                                                 transition.motion, transition.reset_accumulation,
                                                 occlusion,         reflections,
                                                 local_reflections};
    const bool focus_enabled = meshes_ && scene.settings.depth_of_field.enabled &&
                               scene.settings.depth_of_field.blur_radius != 0;
    const bool effects_enabled = temporal_enabled || focus_enabled;
    const auto epoch = meshes_ ? meshes_->resource_epoch() : 0;
    constexpr std::uint64_t temporal_budget = 512ull * 1024 * 1024;
    bool reset_temporal =
        temporal_scene_ != scene.scene || temporal_epoch_ != epoch ||
        temporal_extent_ != std::array<unsigned, 2>{width, height} ||
        temporal_settings_ != scene.settings || pending_resources || temporal_was_pending_ ||
        transition.reset_geometry ||
        (!sample && (!temporal_time_ || !std::isfinite(seconds) || seconds < *temporal_time_ ||
                     seconds - *temporal_time_ > .5));
    bool same_cameras = camera_effects_.size() == cameras.size();
    if (same_cameras)
        for (std::size_t i = 0; i < cameras.size(); ++i)
            same_cameras &= camera_effects_[i].entity == cameras[i].entity;
    if (!effects_enabled || !same_cameras) {
        camera_effects_.clear();
        if (effects_enabled) {
            camera_effects_.reserve(cameras.size());
            for (const auto& camera : cameras)
                camera_effects_.push_back(
                    {camera.entity, camera.view, camera.settings.layers, {}, {}, false});
        }
        reset_temporal = true;
    }
    // A frame failure never publishes partial view or geometry history.
    bool frame_completed = false;
    struct TemporalGuard {
        FrameRenderer& owner;
        bool& completed;
        ~TemporalGuard() {
            if (completed)
                return;
            for (auto& camera : owner.camera_effects_)
                if (camera.history)
                    camera.history->invalidate();
            if (owner.meshes_)
                owner.meshes_->commit_temporal_frame(false);
            owner.temporal_time_.reset();
            owner.temporal_sample_.reset();
            owner.exposure_time_.reset();
        }
    } temporal_guard{*this, frame_completed};
    std::uint64_t temporal_bytes = 0;
    std::uint64_t focus_bytes = 0;
    std::size_t camera_index = 0;
    auto* rtv = color_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    {
        auto timing = presentation_.gpu_scope(context, profile_view_.c_str(), "Frame clear");
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    for (const auto& camera : cameras) {
        CameraEffects* effects = effects_enabled ? &camera_effects_[camera_index] : nullptr;
        CameraEffects* temporal_camera = temporal_enabled ? effects : nullptr;
        ++camera_index;
        if (effects) {
            effects->applied = false;
            if (!temporal_enabled)
                effects->history.reset();
            if (!focus_enabled)
                effects->focus.reset();
        }
        const auto& area = camera.view.viewport;
        if (!area.width || !area.height || area.x >= width || area.y >= height ||
            area.width > width - area.x || area.height > height - area.y)
            throw std::runtime_error("Prepared camera viewport is outside its target");
        validate_camera(camera.settings);
        if (meshes_) {
            auto timing = presentation_.gpu_scope(context, profile_view_.c_str(), "Shadows",
                                                  scene.settings.shadows.enabled);
            meshes_->shadows(scene, camera.view, camera.settings.layers);
        }
        {
            auto timing =
                presentation_.gpu_scope(context, profile_view_.c_str(), "Camera geometry");
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Diligent::Viewport viewport{
                float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
            context->SetViewports(1, &viewport, width, height);
            // Native ClearRenderTarget/ClearDepthStencil always clear the whole view.
            // A clear draw uses this camera rectangle, preserving earlier cameras.
            clear_camera(context, camera.settings);
            if (camera.settings.clear_color)
                sky_.draw(context, camera.view, scene.settings.environment);
            std::optional<MeshSceneRenderer::TemporalPass> temporal_pass;
            std::optional<MeshSceneRenderer::PostMeshPass> focus_pass;
            if (focus_enabled) {
                auto& pass = focus_pass.emplace();
                pass.prepare = [&] {
                    const auto pixels = std::uint64_t(area.width) * area.height;
                    if (!camera.settings.clear_color || !camera.settings.clear_depth)
                        throw std::runtime_error("Camera must clear both color and depth");
                    if (camera_index > 8 ||
                        pixels > (depth_of_field_payload_limit - focus_bytes) / 16)
                        throw std::runtime_error(
                            "Views exceed the eight-camera / 128 MiB focus texture budget");
                    focus_bytes += pixels * 16;
                    std::unique_ptr<DepthOfField> staged;
                    auto* focus = effects->focus.get();
                    if (!focus || effects->unjittered.viewport.width != area.width ||
                        effects->unjittered.viewport.height != area.height) {
                        staged = std::make_unique<DepthOfField>(presentation_, pixels * 16);
                        focus = staged.get();
                    }
                    focus->prepare(color_, depth_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                   camera.view, scene.settings.depth_of_field);
                    if (staged)
                        effects->focus = std::move(staged);
                };
                pass.resolve = [&] {
                    auto timing =
                        presentation_.gpu_scope(context, profile_view_.c_str(), "Depth of field");
                    effects->focus->apply(context, color_,
                                          depth_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                          camera.view, scene.settings.depth_of_field);
                };
            }
            if (temporal_camera) {
                try {
                    const auto bytes =
                        TemporalView::payload_bytes(area.width, area.height, temporal_options);
                    if (!camera.settings.clear_color || !camera.settings.clear_depth)
                        throw std::runtime_error("Camera must clear both color and depth");
                    if (camera_index > 8 || bytes > temporal_budget - temporal_bytes)
                        throw std::runtime_error(
                            "Views exceed the eight-camera / 512 MiB temporal texture budget");
                    temporal_bytes += bytes;
                    auto& state = *temporal_camera;
                    const auto& old = state.unjittered;
                    const bool reset = reset_temporal ||
                                       TemporalView::discontinuous(old, camera.view) ||
                                       state.layers != camera.settings.layers;
                    if (!state.history)
                        state.history =
                            std::make_unique<TemporalView>(presentation_, temporal_budget);
                    // This owner stages extent/mode changes within the admitted view budget.
                    state.history->prepare(camera.view, reset, temporal_options);
                    auto& pass = temporal_pass.emplace();
                    pass.current = state.history->current();
                    pass.previous = state.history->previous();
                    pass.jitter = state.history->jitter();
                    pass.previous_jitter = state.history->previous_jitter();
                    pass.valid = state.history->valid();
                    pass.motion_output = temporal_options.motion_output();
                    pass.indirect_output = temporal_options.indirect_output();
                    pass.reflection_output = temporal_options.reflection_output();
                    pass.normal_only_output = temporal_options.normal_only_output();
                    pass.previous_parts = &state.history->previous_parts();
                    pass.current_parts = &state.history->pending_parts();
                    pass.begin = [&, history = state.history.get()] {
                        history->begin(context, color_, dsv);
                    };
                    pass.resolve = [&, history = state.history.get()](bool complete) {
                        const auto probes = meshes_->reflection_probes(camera.view);
                        history->resolve(context, temporal_settings.stability, complete, probes);
                    };
                } catch (const std::exception& error) {
                    temporal_camera->history.reset();
                    Diagnostic value{Severity::Warning,
                                     "render.temporal.unavailable",
                                     std::string("Temporal surface effects unavailable: ") +
                                         error.what(),
                                     {}};
                    value.context.entity = camera.entity;
                    value.context.asset = scene.scene;
                    value.context.source = "presentation";
                    report(value);
                    temporal_pass.reset();
                }
            }
            if (meshes_)
                meshes_->draw(scene, camera.view, camera.settings.layers, color_, dsv,
                              temporal_pass ? &*temporal_pass : nullptr,
                              focus_pass ? &*focus_pass : nullptr);
            if (focus_pass && !focus_pass->applied)
                effects->focus.reset();
            if (temporal_camera) {
                temporal_camera->applied =
                    temporal_pass && temporal_pass->applied && temporal_pass->successful;
                if (!temporal_camera->applied && temporal_camera->history)
                    temporal_camera->history->invalidate();
            }
        }
        {
            auto timing = presentation_.gpu_scope(context, profile_view_.c_str(), "Bloom",
                                                  scene.settings.bloom.enabled &&
                                                      scene.settings.bloom.intensity != 0);
            bloom_.apply(context, color_, area, scene.settings.bloom);
        }
    }
    if (meshes_) {
        for (const auto& error : meshes_->diagnostics())
            report(error);
        omitted_ += meshes_->omitted_diagnostics();
    } else {
        for (const auto& error : scene.diagnostics)
            report(error);
        omitted_ += scene.omitted_diagnostics;
        if (!scene.meshes.empty() || !scene.particles.empty() || !scene.sprites.empty() ||
            !scene.tile_maps.empty()) {
            Diagnostic error{Severity::Error,
                             "render.resources.unavailable",
                             "Frame has renderable objects but no presentation resource host",
                             {}};
            error.context.asset = scene.scene;
            error.context.source = "presentation";
            report(error);
        }
    }
    ++frames;
    if (output == Output::LinearHdrOpaque) {
        auto* target = color_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &target, depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0.f, 0.f, float(width), float(height), 0.f, 1.f};
        context->SetViewports(1, &viewport, width, height);
        clear_camera(context, Camera{}, true);
    }
    auto* result = color_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    if (output == Output::Display) {
        auto timing = presentation_.gpu_scope(context, profile_view_.c_str(), "Display resolve");
        result =
            display_.resolve(context, result, exposure, scene.settings.color_adjustments,
                             automatic ? *automatic : SceneAutoExposure{}, exposure_timing.delta,
                             reset_exposure, scene.settings.fast_approximate_anti_aliasing);
    }
    if (adaptive) {
        exposure_views_.resize(cameras.size());
        for (std::size_t i = 0; i < cameras.size(); ++i)
            exposure_views_[i] = {cameras[i].entity, cameras[i].view.viewport,
                                  cameras[i].settings.projection};
        exposure_time_ = seconds;
        exposure_scene_ = scene.scene;
        exposure_extent_ = {width, height};
        exposure_was_pending_ = pending_resources;
    } else {
        exposure_time_.reset();
        exposure_views_.clear();
        exposure_was_pending_ = false;
    }
    bool any_temporal = false;
    for (std::size_t i = 0; i < camera_effects_.size(); ++i) {
        auto& camera = camera_effects_[i];
        if (camera.applied) {
            camera.history->complete();
            any_temporal = true;
        }
        camera.unjittered = cameras[i].view;
        camera.layers = cameras[i].settings.layers;
    }
    if (meshes_)
        meshes_->commit_temporal_frame(any_temporal && (aa_enabled || blur_enabled));
    if (!render_textures && render_textures_)
        render_textures_->submit();
    temporal_scene_ = scene.scene;
    temporal_settings_ = scene.settings;
    temporal_epoch_ = epoch;
    temporal_extent_ = {width, height};
    temporal_was_pending_ = pending_resources;
    temporal_time_ =
        std::isfinite(seconds) && seconds >= 0 ? std::optional<double>(seconds) : std::nullopt;
    temporal_sample_ = sample_key;
    frame_completed = true;
    output_ = result;
    return result;
}
bool FrameRenderer::temporal_ready() const {
    if (!temporal_settings_.temporal_anti_aliasing.enabled)
        return true;
    return std::all_of(camera_effects_.begin(), camera_effects_.end(), [](const auto& camera) {
        return !camera.applied || (camera.history && camera.history->samples() >= 8);
    });
}
ITextureView* FrameRenderer::depth() const {
    return depth_ ? depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
}
} // namespace forge
