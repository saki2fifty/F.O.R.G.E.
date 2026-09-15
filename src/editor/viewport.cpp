#include "viewport.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include <stdexcept>
using namespace Diligent;
namespace forge {
Viewport::Viewport(IRenderDevice* device) : device_(device) {
    const char* vs = R"(
cbuffer ObjectData { float4 centerAspect; };
struct Out { float4 position : SV_POSITION; float3 color : COLOR0; };
Out main(uint id : SV_VertexID) {
    float3 v[8]={float3(-1,-1,-1),float3(-1,1,-1),float3(1,1,-1),float3(1,-1,-1),float3(-1,-1,1),float3(-1,1,1),float3(1,1,1),float3(1,-1,1)};
    uint indices[36]={2,0,1,2,3,0,4,6,5,4,7,6,0,7,4,0,3,7,1,0,4,1,4,5,1,5,2,5,6,2,3,6,7,3,2,6};
    float3 p=v[indices[id]]*0.5+centerAspect.xyz+float3(0,-1,6);
    Out o; o.position=float4(p.x/centerAspect.w,p.y,p.z*1.001-0.1001,p.z);
    o.color=float3(0.2,0.6,0.7)*(0.6+0.4*float(id/6)/5); return o;
})";
    const char* ps =
        "float4 main(float4 p:SV_POSITION,float3 color:COLOR0):SV_TARGET{return float4(color,1);}";
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    RefCntAutoPtr<IShader> vertex, pixel;
    shader.Desc.Name = "FORGE block preview VS";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = vs;
    device_->CreateShader(shader, &vertex);
    shader.Desc.Name = "FORGE block preview PS";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = ps;
    device_->CreateShader(shader, &pixel);
    if (!vertex || !pixel)
        throw std::runtime_error("Preview shader compilation failed");
    GraphicsPipelineStateCreateInfo pso;
    pso.PSODesc.Name = "FORGE block preview";
    pso.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    pso.GraphicsPipeline.NumRenderTargets = 1;
    pso.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    pso.GraphicsPipeline.DSVFormat = TEX_FORMAT_D32_FLOAT;
    pso.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pso.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    pso.pVS = vertex;
    pso.pPS = pixel;
    device_->CreateGraphicsPipelineState(pso, &pipeline_);
    if (!pipeline_)
        throw std::runtime_error("Preview pipeline creation failed");
    BufferDesc buffer;
    buffer.Name = "FORGE preview position";
    buffer.Size = 16;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    device_->CreateBuffer(buffer, nullptr, &constants_);
    if (!constants_)
        throw std::runtime_error("Preview constant buffer creation failed");
    auto* variable = pipeline_->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ObjectData");
    if (!variable)
        throw std::runtime_error("Preview shader constants missing");
    variable->Set(constants_);
    pipeline_->CreateShaderResourceBinding(&resources_, true);
}
ITextureView* Viewport::render(IDeviceContext* context, const Json& scene, unsigned width,
                               unsigned height) {
    if (!color_ || color_->GetDesc().Width != width || color_->GetDesc().Height != height) {
        color_.Release();
        depth_.Release();
        TextureDesc t;
        t.Name = "FORGE scene viewport";
        t.Type = RESOURCE_DIM_TEX_2D;
        t.Width = width;
        t.Height = height;
        t.Format = TEX_FORMAT_RGBA8_UNORM;
        t.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device_->CreateTexture(t, nullptr, &color_);
        t.Name = "FORGE scene depth";
        t.Format = TEX_FORMAT_D32_FLOAT;
        t.BindFlags = BIND_DEPTH_STENCIL;
        device_->CreateTexture(t, nullptr, &depth_);
        if (!color_ || !depth_)
            throw std::runtime_error("Viewport allocation failed");
    }
    auto* rtv = color_->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto* dsv = depth_->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float clear[] = {0.025f, 0.04f, 0.055f, 1};
    context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetPipelineState(pipeline_);
    for (const auto& entity : scene.at("entities")) {
        if (entity.value("prefab", false) || !entity.at("components").contains("forge.position"))
            continue;
        const auto& p = entity.at("components").at("forge.position");
        {
            MapHelper<float> data(context, constants_, MAP_WRITE, MAP_FLAG_DISCARD);
            data[0] = p.at("x").get<float>();
            data[1] = p.at("y").get<float>();
            data[2] = p.at("z").get<float>();
            data[3] = float(width) / float(height);
        }
        context->CommitShaderResources(resources_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawAttribs draw;
        draw.NumVertices = 36;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
    }
    return color_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}
} // namespace forge
