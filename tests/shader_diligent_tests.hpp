#pragma once
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "shader_diligent.hpp"
#include <chrono>
#include <functional>
#include <iostream>
namespace forge::test {
// Compilation, reflection, cooked reload and actual graphics/compute use share
// the same WARP device as the viewport acceptance fixture.
inline void
diligent_shaders(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                 const std::function<void(Diligent::ITextureView*, const std::string&)>& capture) {
    using namespace Diligent;
    using namespace asset_detail;
    auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto rejected = [&](auto&& operation) {
        bool bad = false;
        try {
            operation();
        } catch (const std::exception&) {
            bad = true;
        }
        check(bad, "Invalid shader replacement was admitted");
    };
    const char* graphics = R"(
#include "lib/common.hlsli"
struct Vertex { float4 position : SV_POSITION; };
Vertex vs(uint id : SV_VertexID) {
    float2 points[3] = {float2(-.8,-.8),float2(0,.8),float2(.8,-.8)};
    Vertex v; v.position=float4(points[id],.5,1); return v;
}
float4 ps(Vertex v) : SV_TARGET { return fixture_color(); }
[maxvertexcount(3)]
void gs(triangle Vertex input[3],inout TriangleStream<Vertex> output) {
    output.Append(input[0]);output.Append(input[1]);output.Append(input[2]);
}
struct Factors { float edges[3] : SV_TessFactor; float inside : SV_InsideTessFactor; };
Factors constants(InputPatch<Vertex,3> patch) {
    Factors f;f.edges[0]=f.edges[1]=f.edges[2]=2;f.inside=2;return f;
}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")]
[outputcontrolpoints(3)][patchconstantfunc("constants")]
Vertex hs(InputPatch<Vertex,3> patch,uint i:SV_OutputControlPointID) { return patch[i]; }
[domain("tri")]
Vertex ds(Factors f,float3 uv:SV_DomainLocation,const OutputPatch<Vertex,3> patch) {
    Vertex v;v.position=patch[0].position*uv.x+patch[1].position*uv.y+patch[2].position*uv.z;return v;
}
)";
    ShaderSources sources{{"fixtures/main.hlsl", graphics},
                          {"fixtures/lib/common.hlsli", "#include \"../color.hlsli\"\n"},
                          {"fixtures/color.hlsli", R"(
#ifndef FIXTURE_COLOR
#define FIXTURE_COLOR
#if WARM
float4 fixture_color(){return float4(1,.25,0,1);}
#else
float4 fixture_color(){return float4(0,.25,1,1);}
#endif
#endif
)"}};
    ShaderProgramSource source{{{ShaderStage::Vertex, "fixtures/main.hlsl", "vs"},
                                {ShaderStage::Pixel, "fixtures/main.hlsl", "ps"}},
                               {},
                               {{"WARM", {"0", "1"}}}};
    // Separate pure compilation from cooked realization and end-to-end importer
    // timings. No FORGE DDC participates; driver/OS caches are not controlled.
    Json measurements = Json::array();
    for (unsigned sample = 0; sample < 6; ++sample) {
        const auto begin = std::chrono::steady_clock::now();
        auto compiled = compile_diligent_shader(device, source, sources, {{"WARM", "1"}});
        const auto end = std::chrono::steady_clock::now();
        const auto bytes = encode_shader(compiled.data);
        const auto load_begin = std::chrono::steady_clock::now();
        auto loaded = realize_diligent_shader(device, decode_shader(bytes));
        const auto load_end = std::chrono::steady_clock::now();
        check(!loaded.stages.empty(), "Benchmark cooked shader did not realize");
        measurements.push_back(
            {{"sample", sample},
             {"compile_ms", std::chrono::duration<double, std::milli>(end - begin).count()},
             {"cooked_realize_ms",
              std::chrono::duration<double, std::milli>(load_end - load_begin).count()}});
    }
    std::cout << Json{{"benchmark", "shader-compile-and-cooked-reuse"},
                      {"profile", "D3D12/WARP/FXC-5.1"},
                      {"compiler_digest", diligent_shader_compiler_digest()},
                      {"debug", diligent_shader_compiler_debug()},
                      {"workload", "vertex+pixel, two includes, WARM=1"},
                      {"cache", "FORGE DDC bypassed; sample0 first workload compile in process,1-5 "
                                "repeat; OS/driver cache uncontrolled"},
                      {"samples", measurements}}
                     .dump()
              << '\n';
    auto selected = compile_diligent_shader(device, source, sources, {{"WARM", "1"}});
    const auto before = selected.data.build_key;
    const auto cooked = encode_shader(selected.data);
    auto loaded = realize_diligent_shader(device, decode_shader(cooked));
    check(loaded.data.layout_digest() == selected.data.layout_digest(),
          "Cooked shader layout changed");
    {
        ShaderProgramSource surface;
        surface.stages = {{ShaderStage::Pixel, "surface/main.hlsl", "Shade"}};
        surface.surface.emplace();
        surface.surface->uv_sets = {0};
        surface.surface->parameters["tint"] = {MaterialParameterType::LinearColor4,
                                               {1, .25f, .5f, 1}};
        surface.surface->textures["color"].semantic = TextureSemantic::Color;
        ShaderSources body{{"surface/main.hlsl",
                            "float4 Shade(ForgeSurfaceInput input){return "
                            "ForgeParameter_tint()*ForgeSample_color(input);}"}};
        const auto native_surface = compile_diligent_shader(device, surface, body, {});
        auto realized =
            realize_diligent_shader(device, decode_shader(encode_shader(native_surface.data)));
        check(realized.stages.size() == 2 &&
                  realized.stages.contains({ShaderStage::Pixel, ShaderEntryRole::SurfaceColor}) &&
                  realized.stages.contains({ShaderStage::Pixel, ShaderEntryRole::SurfaceDepth}),
              "Cooked surface lost color/depth programs");
        for (const auto& stage : native_surface.data.stages) {
            const auto binding = surface_binding_layout(*surface.surface, stage.reflection);
            check(binding.parameter_bytes == 16 && binding.settings && binding.uv_bytes == 32 &&
                      binding.sampler_count == 1 &&
                      binding.textures == std::vector<std::string>{"color"},
                  "Native surface reflection does not match material bindings");
        }
        auto broken_surface = body;
        broken_surface["surface/main.hlsl"] =
            "float4 Shade(ForgeSurfaceInput input){return Missing();}";
        rejected([&] { compile_diligent_shader(device, surface, broken_surface, {}); });
        check(realized.data.layout_digest() == native_surface.data.layout_digest(),
              "Rejected surface compile changed selected program");
    }
    auto broken = sources;
    broken.at("fixtures/color.hlsli") = "not valid hlsl @";
    rejected([&] { compile_diligent_shader(device, source, broken, {{"WARM", "1"}}); });
    check(selected.data.build_key == before && selected.stages.at(ShaderStage::Pixel),
          "Failed compile replaced selected shader");
    broken = sources;
    // A captured root file must not make an above-root path valid.
    broken["outside.hlsli"] = sources.at("fixtures/color.hlsli");
    broken.at("fixtures/lib/common.hlsli") = "#include \"../../../../outside.hlsli\"\n";
    rejected([&] { compile_diligent_shader(device, source, broken, {{"WARM", "1"}}); });
    auto stale = selected.data;
    stale.stages.front().reflection["resources"].push_back({{"name", "Fake"},
                                                            {"kind", "sampler"},
                                                            {"register", 0},
                                                            {"space", 0},
                                                            {"array_size", 1},
                                                            {"dimension", "unknown"}});
    rejected([&] { realize_diligent_shader(device, stale); });
    // Reflection exercise: struct, matrix, texture, sampler, nonzero spaces and
    // registers. The native adapter supplements only shape/version fields absent
    // from Diligent's public projection; Diligent owns member reflection.
    auto reflected_source = source;
    reflected_source.permutations.clear();
    ShaderSources reflected{{"fixtures/main.hlsl", R"(
struct Vertex {float4 position:SV_POSITION;};
struct Details {float roughness;float3 direction;};
cbuffer Material : register(b2,space1) {float4 Color;Details Info;float4x4 Transform;};
Texture2D Albedo : register(t3,space2);
SamplerState Surface : register(s1,space2);
Vertex vs(uint id:SV_VertexID){Vertex v;v.position=float4(id,0,0,1);return v;}
float4 ps(Vertex v):SV_TARGET {
    return Albedo.Sample(Surface,v.position.xy)*Color*Info.roughness+
           mul(Transform,float4(Info.direction,1));
}
)"}};
    const auto native = compile_diligent_shader(device, reflected_source, reflected, {});
    const auto& resources = native.data.stages.at(1).reflection.at("resources");
    check(resources.size() == 3, "Native shader resource reflection lost bindings");
    bool buffer = false, texture = false, sampler = false;
    for (const auto& r : resources) {
        if (r.at("name") == "Material") {
            buffer = r.at("register") == 2 && r.at("space") == 1 && r.at("variables").size() == 3;
            check(r.at("variables")[1].at("class") == "struct" &&
                      r.at("variables")[2].at("class") == "matrix-rows",
                  "Native member reflection shape mismatch");
        }
        if (r.at("name") == "Albedo")
            texture = r.at("dimension") == "texture2d" && r.at("register") == 3;
        if (r.at("name") == "Surface")
            sampler = r.at("kind") == "sampler" && r.at("space") == 2;
    }
    check(buffer && texture && sampler, "Native shader layout/shape/register extraction failed");
    (void)realize_diligent_shader(device, decode_shader(encode_shader(native.data)));
    auto render = [&](const DiligentShaderProgram& program, const std::string& name,
                      bool tess = false) {
        GraphicsPipelineStateCreateInfo p;
        p.PSODesc.Name = "FORGE shader asset acceptance";
        p.pVS = program.stages.at(ShaderStage::Vertex);
        p.pPS = program.stages.at(ShaderStage::Pixel);
        if (program.stages.contains(ShaderStage::Geometry))
            p.pGS = program.stages.at(ShaderStage::Geometry);
        if (tess) {
            p.pHS = program.stages.at(ShaderStage::Hull);
            p.pDS = program.stages.at(ShaderStage::Domain);
        }
        p.GraphicsPipeline.NumRenderTargets = 1;
        p.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
        p.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
        p.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
        p.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
        p.GraphicsPipeline.PrimitiveTopology =
            tess ? PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        RefCntAutoPtr<IPipelineState> pipeline;
        device->CreateGraphicsPipelineState(p, &pipeline);
        check(bool(pipeline), "Compiled shader stage pipeline creation failed");
        TextureDesc td;
        td.Name = "FORGE shader acceptance target";
        td.Type = RESOURCE_DIM_TEX_2D;
        td.Width = td.Height = 64;
        td.Format = TEX_FORMAT_RGBA8_UNORM;
        td.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> target;
        device->CreateTexture(td, nullptr, &target);
        check(bool(target), "Shader acceptance target allocation failed");
        auto* rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetPipelineState(pipeline);
        DrawAttribs draw{3, DRAW_FLAG_VERIFY_ALL};
        context->Draw(draw);
        capture(target->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), name);
    };
    render(selected, "shader-source-last-good");
    render(loaded, "shader-cooked");
    auto geometry = source;
    geometry.stages.push_back({ShaderStage::Geometry, "fixtures/main.hlsl", "gs"});
    render(compile_diligent_shader(device, geometry, sources, {{"WARM", "1"}}), "shader-geometry");
    auto tess = source;
    tess.stages.push_back({ShaderStage::Hull, "fixtures/main.hlsl", "hs"});
    tess.stages.push_back({ShaderStage::Domain, "fixtures/main.hlsl", "ds"});
    render(compile_diligent_shader(device, tess, sources, {{"WARM", "1"}}), "shader-tessellation",
           true);
    ShaderProgramSource compute{{{ShaderStage::Compute, "compute.hlsl", "main"}}};
    ShaderSources compute_sources{{"compute.hlsl", R"(
RWTexture2D<float4> Destination;
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){Destination[id.xy]=float4(1,.25,0,1);}
)"}};
    const auto cs = compile_diligent_shader(device, compute, compute_sources, {});
    check(cs.data.stages[0].reflection.at("threads") == nlohmann::json{8, 8, 1},
          "Compute group reflection failed");
    ComputePipelineStateCreateInfo cp;
    cp.PSODesc.Name = "FORGE cooked compute acceptance";
    cp.pCS = cs.stages.at(ShaderStage::Compute);
    RefCntAutoPtr<IPipelineState> pipeline;
    device->CreateComputePipelineState(cp, &pipeline);
    check(bool(pipeline), "Compute pipeline creation failed");
    TextureDesc td;
    td.Name = "FORGE compute acceptance target";
    td.Type = RESOURCE_DIM_TEX_2D;
    td.Width = td.Height = 64;
    td.Format = TEX_FORMAT_RGBA8_UNORM;
    td.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> target;
    device->CreateTexture(td, nullptr, &target);
    check(bool(target), "Compute target allocation failed");
    auto* variable = pipeline->GetStaticVariableByName(SHADER_TYPE_COMPUTE, "Destination");
    check(variable != nullptr, "Compute destination binding missing");
    variable->Set(target->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS));
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->DispatchCompute({8, 8, 1});
    capture(target->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), "shader-compute");
}
} // namespace forge::test
