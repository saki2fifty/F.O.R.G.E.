#include "frame_renderer.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
namespace forge {
using namespace Diligent;
FrameRenderer::FrameRenderer(DiligentPresentation& presentation)
    : presentation_(presentation), display_(presentation),
      sky_(presentation, TEX_FORMAT_RGBA16_FLOAT) {
    ShaderCreateInfo ci;
    ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    ci.HLSLVersion = {5, 1};
    ci.ShaderCompiler = SHADER_COMPILER_FXC;
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
    for (unsigned mask = 1; mask <= 3; ++mask) {
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
            (mask & 1) ? COLOR_MASK_ALL : COLOR_MASK_NONE;
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
void FrameRenderer::resources(std::shared_ptr<MeshResourceHost> host) {
    if (thread_ != std::this_thread::get_id())
        throw std::runtime_error("Frame resources changed off presentation thread");
    sky_.select({});
    meshes_ = host ? std::make_unique<MeshSceneRenderer>(std::move(host), TEX_FORMAT_RGBA16_FLOAT)
                   : nullptr;
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
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation_.device()->CreateTexture(desc, nullptr, &depth);
    if (!color || !depth)
        throw std::runtime_error("Frame allocation failed; previous targets retained");
    color_ = std::move(color);
    depth_ = std::move(depth);
}
void FrameRenderer::clear_camera(IDeviceContext* context, const Camera& settings) {
    const unsigned mask = unsigned(settings.clear_color) | (unsigned(settings.clear_depth) << 1);
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
                                  unsigned width, unsigned height) {
    const auto scene = extract_render_scene(source);
    const auto cameras = prepare_game_cameras(scene, width, height);
    auto* result = render(context, scene, cameras.cameras, width, height, scene.settings.exposure);
    for (const auto& error : cameras.diagnostics)
        report(error);
    omitted_ += cameras.omitted_diagnostics;
    return result;
}
ITextureView* FrameRenderer::render(IDeviceContext* context, const RenderScene& scene,
                                    std::span<const PreparedCamera> cameras, unsigned width,
                                    unsigned height, float exposure) {
    if (thread_ != std::this_thread::get_id() || !context)
        throw std::runtime_error("Frame rendering requires its presentation thread and context");
    targets(width, height);
    if (cameras.data() != cameras_.data())
        cameras_.assign(cameras.begin(), cameras.end());
    diagnostics_.clear();
    omitted_ = 0;
    if (meshes_) {
        meshes_->update(scene);
        sky_.select(meshes_->environment());
    }
    auto* rtv = color_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float black[]{0, 0, 0, 1};
    context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    for (const auto& camera : cameras) {
        const auto& area = camera.view.viewport;
        if (!area.width || !area.height || area.x >= width || area.y >= height ||
            area.width > width - area.x || area.height > height - area.y)
            throw std::runtime_error("Prepared camera viewport is outside its target");
        validate_camera(camera.settings);
        if (meshes_)
            meshes_->shadows(scene, camera.view, camera.settings.layers);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{
            float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        // Native ClearRenderTarget/ClearDepthStencil always clear the whole view.
        // A clear draw uses this camera rectangle, preserving earlier cameras.
        clear_camera(context, camera.settings);
        if (camera.settings.clear_color)
            sky_.draw(context, camera.view, scene.settings.environment);
        if (meshes_)
            meshes_->draw(scene, camera.view, camera.settings.layers);
    }
    if (meshes_) {
        for (const auto& error : meshes_->diagnostics())
            report(error);
        omitted_ += meshes_->omitted_diagnostics();
    } else {
        for (const auto& error : scene.diagnostics)
            report(error);
        omitted_ += scene.omitted_diagnostics;
        if (!scene.meshes.empty()) {
            Diagnostic error{Severity::Error,
                             "render.resources.unavailable",
                             "Frame has meshes but no presentation resource host",
                             {}};
            error.context.asset = scene.scene;
            error.context.source = "presentation";
            report(error);
        }
    }
    ++frames;
    return display_.resolve(context, color_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                            exposure);
}
ITextureView* FrameRenderer::depth() const {
    return depth_ ? depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
}
} // namespace forge
