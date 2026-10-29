#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "presentation_views.hpp"
#include "render_projection.hpp"
#include "surface_depth_shader.hpp"
#include "surface_reflections.hpp"
#include <algorithm>
namespace forge {
using namespace Diligent;
void SurfaceReflections::validate_sources(const SurfaceInput& input,
                                          std::span<const SourceLevel> levels) const {
    if (!complete_surface_view(input.color, TEX_FORMAT_RGBA16_FLOAT,
                               TEXTURE_VIEW_SHADER_RESOURCE) ||
        !complete_surface_view(input.depth, TEX_FORMAT_D32_FLOAT, TEXTURE_VIEW_SHADER_RESOURCE))
        throw std::invalid_argument("Reflection sources require complete HDR and depth inputs");
    const auto& size = input.color->GetTexture()->GetDesc();
    const auto& depth = input.depth->GetTexture()->GetDesc();
    if (depth.Width != size.Width || depth.Height != size.Height)
        throw std::invalid_argument("Reflection source HDR and depth extents must match");
    const auto plan = reflection_source_plan(size.Width, size.Height);
    if (levels.size() != plan.count)
        throw std::invalid_argument("Reflection source level count does not match its extent");
    std::array<ITexture*, reflection_source_level_limit * 2> admitted{};
    unsigned count = 0;
    for (unsigned i = 0; i < plan.count; ++i) {
        const auto validate = [&](ITexture* texture, TEXTURE_FORMAT format) {
            if (!texture ||
                !complete_surface_view(texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                       format, TEXTURE_VIEW_SHADER_RESOURCE) ||
                !complete_surface_view(texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), format,
                                       TEXTURE_VIEW_RENDER_TARGET) ||
                texture->GetDesc().Width != plan.levels[i].width ||
                texture->GetDesc().Height != plan.levels[i].height ||
                std::find(admitted.begin(), admitted.begin() + count, texture) !=
                    admitted.begin() + count)
                throw std::invalid_argument(
                    "Reflection sources require distinct complete targets at each planned extent");
            for (auto* source : {input.color, input.indirect, input.depth, input.surface,
                                 input.response, input.specular})
                if (source && source->GetTexture() == texture)
                    throw std::invalid_argument("Reflection source target aliases a current input");
            admitted[count++] = texture;
        };
        validate(levels[i].color, TEX_FORMAT_RGBA16_FLOAT);
        validate(levels[i].range, TEX_FORMAT_RG32_FLOAT);
    }
}
void SurfaceReflections::prepare_sources() {
    if (thread_ != std::this_thread::get_id())
        throw std::logic_error("Reflection source preparation requires its presentation thread");
    if (sources_[0].pipeline)
        return;
    // Stage both interfaces before publishing them. All textures belong to the view.
    std::array<PipelineState, 2> staged;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE reflection source triangle";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        return float4(id==2?3:-1,id==1?3:-1,0,1);})";
    RefCntAutoPtr<IShader> vertex;
    presentation_.shader(shader, &vertex);
    const std::string prefix = R"(
Texture2D<float4> SourceColor;
cbuffer SourceValues {float4 Extent;float4 P0;float4 P1;float4 P2;float4 P3;};
struct Result {float4 color:SV_Target0;float2 range:SV_Target1;};
)";
    const std::array<std::string, 2> shaders{prefix + "Texture2D<float> CurrentDepth;\n" +
                                                 std::string(surface_depth_shader) + R"(
Result main(float4 p:SV_Position) {
    Result r;r.color=0;r.range=float2(3.402823e38,0);int2 first=int2(p.xy)*2;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x) {
        int2 at=first+int2(x,y);float3 geometry;if(!positionAt(at,geometry))continue;
        float3 c=SourceColor.Load(int3(at,0)).rgb;
        if(!all(isfinite(c))||any(c<0))continue;
        r.color+=float4(c,1);
        r.range=float2(min(r.range.x,geometry.z),max(r.range.y,geometry.z));
    }
    r.color*=.25;if(r.color.a==0)r.range=0;return r;
})",
                                             prefix + R"(
Texture2D<float2> SourceRange;
Result main(float4 p:SV_Position) {
    Result r;r.color=0;r.range=float2(3.402823e38,0);int2 first=int2(p.xy)*2;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x) {
        int2 at=first+int2(x,y);if(any(at>=int2(Extent.xy)))continue;
        float4 c=SourceColor.Load(int3(at,0));if(c.a<=0)continue;
        float2 d=SourceRange.Load(int3(at,0));r.color+=c;
        r.range=float2(min(r.range.x,d.x),max(r.range.y,d.y));
    }
    r.color*=.25;if(r.color.a==0)r.range=0;return r;
})"};
    for (unsigned i = 0; i < staged.size(); ++i) {
        auto& mode = staged[i];
        shader.Desc.Name = "FORGE weighted reflection source reduction";
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        shader.Source = shaders[i].c_str();
        RefCntAutoPtr<IShader> pixel;
        presentation_.shader(shader, &pixel);
        BufferDesc buffer;
        buffer.Name = "FORGE reflection source dimensions and projection";
        buffer.Size = 80;
        buffer.BindFlags = BIND_UNIFORM_BUFFER;
        buffer.Usage = USAGE_DYNAMIC;
        buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
        presentation_.device()->CreateBuffer(buffer, nullptr, &mode.values);
        if (!mode.values)
            throw std::runtime_error("Reflection source parameter allocation failed");
        const char* depth_name = i == 0 ? "CurrentDepth" : "SourceRange";
        const ShaderResourceVariableDesc variables[]{
            {SHADER_TYPE_PIXEL, "SourceColor", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, depth_name, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE reflection source radiance and depth bounds";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        info.PSODesc.ResourceLayout.Variables = variables;
        info.PSODesc.ResourceLayout.NumVariables = Uint32(std::size(variables));
        auto& graphics = info.GraphicsPipeline;
        graphics.NumRenderTargets = 2;
        graphics.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
        graphics.RTVFormats[1] = TEX_FORMAT_RG32_FLOAT;
        graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        graphics.RasterizerDesc.CullMode = CULL_MODE_NONE;
        graphics.DepthStencilDesc.DepthEnable = false;
        graphics.DepthStencilDesc.DepthWriteEnable = false;
        info.pVS = vertex;
        info.pPS = pixel;
        presentation_.graphics(info, &mode.pipeline);
        if (!mode.pipeline)
            throw std::runtime_error("Reflection source pipeline unavailable");
        mode.pipeline->CreateShaderResourceBinding(&mode.binding, true);
        if (!mode.binding || !mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, "SourceValues") ||
            !mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, "SourceColor") ||
            !mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, depth_name))
            throw std::runtime_error("Reflection source shader interface incomplete");
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, "SourceValues")->Set(mode.values);
    }
    sources_ = std::move(staged);
}
void SurfaceReflections::build_sources(IDeviceContext* context, const SurfaceInput& input,
                                       std::span<const SourceLevel> levels) {
    if (thread_ != std::this_thread::get_id() || !context)
        throw std::invalid_argument("Reflection sources require their presentation thread/context");
    validate_sources(input, levels);
    validate_surface_projection(input.projection);
    if (levels.empty())
        return;
    prepare_sources();
    auto* color = input.color;
    auto* range = input.depth;
    for (unsigned i = 0; i < levels.size(); ++i) {
        auto& mode = sources_[i == 0 ? 0 : 1];
        const auto& size = color->GetTexture()->GetDesc();
        {
            MapHelper<float> values(context, mode.values, MAP_WRITE, MAP_FLAG_DISCARD);
            values[0] = float(size.Width);
            values[1] = float(size.Height);
            values[2] = values[3] = 0;
            for (unsigned c = 0; c < input.projection.size(); ++c)
                values[4 + c] = input.projection[c];
        }
        const char* depth_name = i == 0 ? "CurrentDepth" : "SourceRange";
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, "SourceColor")->Set(color);
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, depth_name)->Set(range);
        const auto& level = levels[i];
        ITextureView* targets[]{level.color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                level.range->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
        const auto& extent = level.color->GetDesc();
        context->SetRenderTargets(2, targets, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{0, 0, float(extent.Width), float(extent.Height), 0, 1};
        context->SetViewports(1, &viewport, extent.Width, extent.Height);
        context->SetPipelineState(mode.pipeline);
        context->CommitShaderResources(mode.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, "SourceColor")->Set(nullptr);
        mode.binding->GetVariableByName(SHADER_TYPE_PIXEL, depth_name)->Set(nullptr);
        color = level.color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        range = level.range->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }
}
} // namespace forge
