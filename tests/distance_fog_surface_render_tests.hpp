#pragma once
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "mesh_draw_shader.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/material_graph.hpp>
#include <forge/surface_shader.hpp>

namespace forge::test_fog_surface {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    const auto compile = [&](SHADER_TYPE stage, const std::string& source, const char* entry) {
        ShaderCreateInfo ci;
        ci.Desc.Name = "FORGE distance fog generated surface fixture";
        ci.Desc.ShaderType = stage;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.Source = source.c_str();
        ci.EntryPoint = entry;
        ci.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        RefCntAutoPtr<IShader> result;
        presentation.shader(ci, &result);
        check(bool(result), "Fog surface compile failed");
        return result;
    };
    const auto insert = [](std::string& source, const std::string& include,
                           const std::string& replacement) {
        const auto at = source.find(include);
        check(at != std::string::npos, "Generated surface fixture lost its engine include");
        source.replace(at, include.size(), replacement);
    };
    const auto vertex = compile(
        SHADER_TYPE_VERTEX,
        "struct Output{float4 Position:SV_Position;float3 World:TEXCOORD0;};"
        "Output main(uint id:SV_VertexID){Output o;o.Position=float4(id==2?3:-1,id==1?3:-1,0,1);"
        "o.World=float3(0,0,2.5);return o;}",
        "main");
    RefCntAutoPtr<ITexture> color;
    TextureDesc desc;
    desc.Name = "FORGE generated surface fog pixels";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 16;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    check(bool(color), "Fog surface target failed");
    const auto buffer = [&](const std::vector<float>& values) {
        BufferDesc desc;
        desc.Name = "FORGE fog surface fixture constants";
        desc.Size = values.size() * sizeof(float);
        desc.Usage = USAGE_IMMUTABLE;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        BufferData data;
        data.pData = values.data();
        data.DataSize = desc.Size;
        RefCntAutoPtr<IBuffer> result;
        device->CreateBuffer(desc, &data, &result);
        check(bool(result), "Fog surface constants failed");
        return result;
    };
    SurfaceShaderDefinition definition;
    definition.uv_sets = {0};
    struct Case {
        const char* expression;
        float alpha_mode, enabled;
        std::array<float, 4> expected;
    };
    for (const auto& item :
         std::array{Case{"float4(.8,.2,.1,.25)", 2, 1, {.45f, .3f, .4f, .25f}},
                    Case{"float4(.8,.2,.1,.25)", 0, 1, {.45f, .3f, .4f, 1}},
                    Case{"float4(.8,.2,.1,.25)", 1, 1, {0, 0, 0, 0}},
                    Case{"float4(.8,.2,.1,.25)", 2, 0, {.8f, .2f, .1f, .25f}},
                    Case{"float4(asfloat(0x7fc00000),0,0,.25)", 2, 1, {1, 0, 1, 1}},
                    Case{"float4(1,0,1,1)", 2, 1, {.55f, .2f, .85f, 1}}}) {
        auto source = surface_shader_wrapper("function.hlsli", "Shade", false);
        insert(source, "#include \"engine/forge.surface.hlsli\"",
               surface_shader_header(definition));
        insert(source, "#include \"function.hlsli\"",
               std::string("float4 Shade(ForgeSurfaceInput input){return ") + item.expression +
                   ";}");
        const auto pixel = compile(SHADER_TYPE_PIXEL, source, "ForgeSurfaceColor");
        const auto fog = buffer({.1f, .4f, .7f, item.enabled, 1, 4, 0, 0});
        const auto state = buffer({item.alpha_mode, .5f, 0, 0});
        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = "FORGE generated surface fog test";
        ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        ci.pVS = vertex;
        ci.pPS = pixel;
        ci.GraphicsPipeline.NumRenderTargets = 1;
        ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
        ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
        ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
        RefCntAutoPtr<IPipelineState> pipeline;
        presentation.graphics(ci, &pipeline);
        RefCntAutoPtr<IShaderResourceBinding> binding;
        pipeline->CreateShaderResourceBinding(&binding, true);
        // A constant invalid user function can optimize fog/state bindings out.
        if (auto* var = binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeDistanceFog"))
            var->Set(fog);
        if (auto* var = binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeSurfaceState"))
            var->Set(state);
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]{};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport viewport{0, 0, 16, 16, 0, 1};
        context->SetViewports(1, &viewport, 16, 16);
        context->SetPipelineState(pipeline);
        context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawAttribs draw;
        draw.NumVertices = 3;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->Draw(draw);
        presentation.end_frame(context);
        const auto pixels = readback(color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        for (const auto& value : pixels)
            for (unsigned lane = 0; lane < 4; ++lane)
                check(std::abs(int(value[lane]) - int(std::lround(item.expected[lane] * 255))) <= 1,
                      "Custom surface fog, alpha or diagnostic pixels differ");
    }
    // Compile actual shared PBR graph color/depth wrappers. This is native
    // generated-source proof; it does not load D3D12 Shader cooks on Vulkan.
    const auto graph = compile_material_graph(create_material_graph());
    for (const bool depth : {false, true}) {
        auto source = material_graph_shader_wrapper(graph.surface, "graph.hlsli", depth);
        insert(source, "#include \"engine/forge.surface.hlsli\"",
               surface_shader_header(graph.surface));
        insert(source, "#include \"graph.hlsli\"", graph.source);
        (void)compile(SHADER_TYPE_PIXEL, source, depth ? "ForgeSurfaceDepth" : "ForgeSurfaceColor");
    }
    std::cout << "Generated custom surface fog/alpha/diagnostic pixels and graph wrappers PASS\n";
}
} // namespace forge::test_fog_surface
