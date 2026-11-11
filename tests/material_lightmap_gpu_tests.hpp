#pragma once
#include "Graphics/GraphicsTools/interface/ShaderSourceFactoryUtils.h"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "material_lightmap_fixture.hpp"
#include "mesh_draw_shader.hpp"
#include <map>
#include <numbers>
#include <tuple>
namespace forge::lightmap_test {
inline void exercise_graph_gpu(DiligentPresentation&, Diligent::IDeviceContext*);
// Source-level native execution, separate from the Windows cooked Shader path.
inline void exercise_gpu(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    using Row = test_bloom::Pixel;
    auto* device = presentation.device();
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE imported irradiance analytical comparison";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = "float4 main(uint id:SV_VertexID):SV_Position{"
                    "return float4(id==2?3:-1,id==1?3:-1,0,1);}";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation.shader(shader, &vertex);
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    shader.Source = R"(
#define USE_IBL 1
#define USE_HDR_IBL_CUBEMAPS 1
#define TEX_COLOR_CONVERSION_MODE 0
#define ENABLE_CLEAR_COAT 0
#define ENABLE_IRIDESCENCE 0
#define ENABLE_SHEEN 0
#define ENABLE_ANISOTROPY 0
#define ENABLE_TRANSMISSION 0
#include "ForgeLighting.fxh"
cbuffer Input {float4 Irradiance;float4 Base;float4 Options;};
float4 main():SV_Target0 {
    SurfaceShadingInfo s=(SurfaceShadingInfo)0;
    s.BaseLayer.Srf=GetSurfaceReflectanceMR(Base.rgb,Options.x,Options.y,float3(1,1,1),1);
    s.Occlusion=Options.z;
    float3 radiance;
    bool valid=ForgeLightmapDiffuse(Irradiance.rgb,s,radiance);
    return float4(radiance,valid?1:0);
}
)";
    presentation.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE imported irradiance native oracle";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    auto& g = info.GraphicsPipeline;
    g.NumRenderTargets = 1;
    g.RTVFormats[0] = TEX_FORMAT_RGBA32_FLOAT;
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.DepthStencilDesc.DepthEnable = false;
    info.pVS = vertex;
    info.pPS = pixel;
    RefCntAutoPtr<IPipelineState> pass;
    presentation.graphics(info, &pass);
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pass->CreateShaderResourceBinding(&binding, true);
    BufferDesc desc;
    desc.Name = "FORGE irradiance oracle inputs";
    desc.Size = 3 * sizeof(Row);
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DEFAULT;
    RefCntAutoPtr<IBuffer> inputs;
    device->CreateBuffer(desc, nullptr, &inputs);
    check(bool(inputs), "Irradiance input allocation failed");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "Input")->Set(inputs);
    TextureDesc image;
    image.Name = "FORGE irradiance float32 response";
    image.Type = RESOURCE_DIM_TEX_2D;
    image.Width = image.Height = 2;
    image.Format = TEX_FORMAT_RGBA32_FLOAT;
    image.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> target;
    device->CreateTexture(image, nullptr, &target);
    check(bool(target), "Irradiance response allocation failed");
    auto evaluate = [&](Row irradiance, Row base, Row options) {
        const std::array<Row, 3> values{irradiance, base, options};
        presentation.begin_frame();
        context->UpdateBuffer(inputs, 0, sizeof(values), values.data(),
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        auto* rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport viewport{0, 0, 2, 2, 0, 1};
        context->SetViewports(1, &viewport, 2, 2);
        context->SetPipelineState(pass);
        context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
        presentation.end_frame(context);
        const auto pixels = test_bloom::read(device, context, target);
        for (const auto& value : pixels)
            check(value == pixels.front(), "Uniform irradiance response varies spatially");
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels.front();
    };
    unsigned cases = 0;
    double worst = 0;
    for (const auto energy : {Row{0, 0, 0, 0}, Row{1, 2, 4, 0}, Row{31, .25f, 7, 0}})
        for (const auto base : {Row{1, 1, 1, 0}, Row{.2f, .7f, .05f, 0}, Row{0, 0, 0, 0}})
            for (float metal : {0.f, .35f, 1.f})
                for (float rough : {0.f, .4f, 1.f})
                    for (float occlusion : {0.f, .25f, 1.f}) {
                        const auto actual = evaluate(energy, base, {metal, rough, occlusion, 0});
                        check(actual[3] == 1, "Valid irradiance rejected");
                        for (unsigned c = 0; c < 3; ++c) {
                            // Dielectric F0=.04, an independent Lambertian scalar oracle.
                            const double expected = double(energy[c]) * base[c] * .96 *
                                                    (1 - double(metal)) * occlusion /
                                                    std::numbers::pi;
                            const auto error = std::abs(actual[c] - expected);
                            worst = std::max(worst, error);
                            check(error <= 2e-6 * std::max(1., expected),
                                  "Irradiance differs from analytical diffuse response");
                        }
                        ++cases;
                    }
    const auto inf = std::numeric_limits<float>::infinity();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    for (const auto invalid : {Row{-1, 0, 0, 0}, Row{0, inf, 0, 0}, Row{0, 0, nan, 0},
                               Row{std::numeric_limits<float>::max(), 1, 1, 0}}) {
        const auto actual = evaluate(invalid, {16, 16, 16, 0}, {0, .5f, 1, 0});
        check(actual == Row{0, 0, 0, 0},
              "Invalid/overflow irradiance escaped into the indirect output");
        ++cases;
    }
    std::cout << "Imported irradiance native source: " << cases
              << " analytical/invalid cases, worst absolute error " << worst << '\n';
    exercise_graph_gpu(presentation, context);
}
inline void exercise_graph_gpu(DiligentPresentation& presentation,
                               Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    using Row = test_bloom::Pixel;
    auto* device = presentation.device();
    auto source = graph();
    const auto surface = *std::find_if(source["nodes"].begin(), source["nodes"].end(),
                                       [](const auto& n) { return n.at("type") == "surface"; });
    for (const auto& [key, type, value] :
         {std::tuple{"metallic", "scalar", nlohmann::json{0}},
          std::tuple{"occlusion", "scalar", nlohmann::json{1}},
          std::tuple{"emissive", "color3", nlohmann::json{.125, .25, .5}}}) {
        auto parameter = create_material_graph_node("parameter", {0, 0});
        parameter["data"]["key"] = key;
        parameter["data"]["type"] = type;
        parameter["data"]["value"] = value;
        source["edges"].push_back({{"id", GraphEdgeId::generate()},
                                   {"from", {{"node", parameter.at("id")}, {"port", "out"}}},
                                   {"to", {{"node", surface.at("id")}, {"port", key}}}});
        source["nodes"].push_back(parameter);
    }
    const auto compiled = compile_material_graph(source);
    const auto header = surface_shader_header(compiled.surface);
    const MemoryShaderSourceFileInfo files[]{{"engine/forge.graph.hlsl", compiled.source.c_str()},
                                             {"engine/forge.surface.hlsli", header.c_str()}};
    RefCntAutoPtr<IShaderSourceInputStreamFactory> memory, factory;
    CreateMemoryShaderSourceFactory({files, 2, false}, &memory);
    IShaderSourceInputStreamFactory* factories[]{
        memory, &DiligentFXShaderSourceStreamFactory::GetInstance()};
    CreateCompoundShaderSourceFactory({factories, 2}, &factory);
    check(memory && factory, "Generated graph source factory unavailable");
    auto pixel_source =
        material_graph_shader_wrapper(compiled.surface, "engine/forge.graph.hlsl", false, true);
    pixel_source += R"(
struct Response {float4 Color:SV_Target0;float4 Indirect:SV_Target1;};
Response main(float4 position:SV_Position) {
    ForgeSurfaceInput input=(ForgeSurfaceInput)0;
    input.Position=position;input.World=float3(position.xy*.01,-2);
    input.Normal=float3(0,0,1);input.Tangent=float3(1,0,0);input.Bitangent=float3(0,1,0);
    input.Color=1;input.FrontFace=true;input.UV[0]=float2(position.x/4,.5);
    Response result;float3 indirect;
    result.Color=ForgeSurfaceIndirectLighting(input,indirect);
    result.Indirect=float4(indirect,0);return result;
}
)";
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE generated imported lightmap graph";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = "float4 main(uint id:SV_VertexID):SV_Position{"
                    "return float4(id==2?3:-1,id==1?3:-1,0,1);}";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation.shader(shader, &vertex);
    shader.Source = pixel_source.c_str();
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.LoadConstantBufferReflection = true;
    shader.pShaderSourceStreamFactory = factory;
    presentation.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE generated graph diffuse and indirect response";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    auto& g = info.GraphicsPipeline;
    g.NumRenderTargets = 2;
    g.RTVFormats[0] = g.RTVFormats[1] = TEX_FORMAT_RGBA32_FLOAT;
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.DepthStencilDesc.DepthEnable = false;
    info.pVS = vertex;
    info.pPS = pixel;
    RefCntAutoPtr<IPipelineState> pass;
    presentation.graphics(info, &pass);
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pass->CreateShaderResourceBinding(&binding, true);
    std::map<std::string, RefCntAutoPtr<IBuffer>> buffers;
    std::map<std::string, std::vector<std::byte>> constants;
    SurfaceBindingLayout layout;
    for (unsigned i = 0; i < pixel->GetResourceCount(); ++i) {
        ShaderResourceDesc resource;
        pixel->GetResourceDesc(i, resource);
        if (resource.Type != SHADER_RESOURCE_TYPE_CONSTANT_BUFFER)
            continue;
        const auto* reflected = pixel->GetConstantBufferDesc(i);
        check(reflected && reflected->Size, "Generated graph buffer reflection missing");
        auto& bytes = constants[resource.Name];
        bytes.resize(reflected->Size);
        if (std::string_view(resource.Name) == "ForgeSurfaceMaterial") {
            layout.parameter_bytes = reflected->Size;
            for (unsigned j = 0; j < reflected->NumVariables; ++j) {
                const auto& variable = reflected->pVariables[j];
                const std::string prefix = "g_SurfaceParameter_";
                check(std::string_view(variable.Name).starts_with(prefix),
                      "Unexpected generated graph parameter");
                layout.parameters.push_back({std::string(variable.Name).substr(prefix.size()),
                                             variable.Offset, variable.NumColumns});
            }
        }
        BufferDesc desc;
        desc.Name = "FORGE generated graph reflected uniforms";
        desc.Size = bytes.size();
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        desc.Usage = USAGE_DEFAULT;
        auto& buffer = buffers[resource.Name];
        device->CreateBuffer(desc, nullptr, &buffer);
        check(bool(buffer), "Generated graph uniform allocation failed");
        binding->GetVariableByName(SHADER_TYPE_PIXEL, resource.Name)->Set(buffer);
    }
    MaterialData pbr;
    pbr.model = "forge.gltf.metallic-roughness.v1";
    const auto builtins = material_shader(prepare_pbr_material(pbr), compiled.surface.uv_sets);
    const auto builtin_bytes = std::as_bytes(std::span(builtins.uniforms));
    // Legacy wrappers reflect these defaults; current wrappers specialize them.
    if (auto found = constants.find("ForgeMaterialValues"); found != constants.end())
        found->second.assign(builtin_bytes.begin(), builtin_bytes.end());
    auto material = surface_material_defaults(compiled.surface);
    material.parameters.at("lightmap_gain").value = {.5f, 1, 2, 0};
    const auto base_key =
        std::find_if(material.parameters.begin(), material.parameters.end(), [](const auto& p) {
            return p.second.type == MaterialParameterType::LinearColor4;
        })->first;
    material.parameters.at(base_key).value = {.2f, .7f, .05f, 1};
    const std::vector<Row> samples{{2, .5f, 4, 1}, {6, 3, 1, 1}};
    auto lightmap = test_bloom::texture(device, 2, 1, samples);
    auto black = test_bloom::texture(device, 1, 1, {{0, 0, 0, 0}});
    TextureDesc cube_desc = black->GetDesc();
    cube_desc.Type = RESOURCE_DIM_TEX_CUBE;
    cube_desc.ArraySize = 6;
    cube_desc.BindFlags = BIND_SHADER_RESOURCE;
    const Row zero{};
    TextureSubResData cube_levels[6];
    for (auto& level : cube_levels) {
        level.pData = zero.data();
        level.Stride = sizeof(Row);
    }
    cube_desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    Diligent::TextureData cube_data{cube_levels, 6};
    RefCntAutoPtr<ITexture> cube, shadow;
    device->CreateTexture(cube_desc, &cube_data, &cube);
    TextureDesc shadow_desc = cube_desc;
    shadow_desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
    shadow_desc.ArraySize = 1;
    shadow_desc.Format = TEX_FORMAT_D32_FLOAT;
    shadow_desc.BindFlags |= BIND_DEPTH_STENCIL;
    device->CreateTexture(shadow_desc, nullptr, &shadow);
    check(cube && shadow, "Generated graph neutral resources unavailable");
    SamplerDesc sampler_desc;
    sampler_desc.MinFilter = sampler_desc.MagFilter = sampler_desc.MipFilter = FILTER_TYPE_POINT;
    sampler_desc.AddressU = sampler_desc.AddressV = sampler_desc.AddressW = TEXTURE_ADDRESS_CLAMP;
    RefCntAutoPtr<ISampler> sampler, comparison;
    device->CreateSampler(sampler_desc, &sampler);
    sampler_desc.MinFilter = sampler_desc.MagFilter = FILTER_TYPE_COMPARISON_POINT;
    sampler_desc.MipFilter = FILTER_TYPE_COMPARISON_POINT;
    sampler_desc.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
    device->CreateSampler(sampler_desc, &comparison);
    for (unsigned i = 0; i < binding->GetVariableCount(SHADER_TYPE_PIXEL); ++i) {
        auto* variable = binding->GetVariableByIndex(SHADER_TYPE_PIXEL, i);
        ShaderResourceDesc resource;
        variable->GetResourceDesc(resource);
        const std::string_view name = resource.Name;
        IDeviceObject* object = nullptr;
        if (resource.Type == SHADER_RESOURCE_TYPE_SAMPLER)
            object = name == "g_ForgeShadowSampler" ? comparison.RawPtr() : sampler.RawPtr();
        else if (resource.Type == SHADER_RESOURCE_TYPE_TEXTURE_SRV) {
            auto* image = name == "g_SurfaceTexture_lightmap"  ? lightmap.RawPtr()
                          : name == "g_ForgeGGX"               ? black.RawPtr()
                          : name.starts_with("g_ForgeShadows") ? shadow.RawPtr()
                                                               : cube.RawPtr();
            object = image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }
        if (object) {
            std::vector<IDeviceObject*> values(resource.ArraySize, object);
            variable->SetArray(values.data(), 0, resource.ArraySize);
        }
    }
    std::array<RefCntAutoPtr<ITexture>, 2> outputs;
    for (auto& output : outputs) {
        TextureDesc desc;
        desc.Name = "FORGE generated lightmap graph float32 output";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = 4;
        desc.Height = 2;
        desc.Format = TEX_FORMAT_RGBA32_FLOAT;
        desc.BindFlags = BIND_RENDER_TARGET;
        device->CreateTexture(desc, nullptr, &output);
        check(bool(output), "Generated graph output allocation failed");
    }
    auto render = [&] {
        presentation.begin_frame();
        for (const auto& [name, bytes] : constants)
            context->UpdateBuffer(buffers.at(name), 0, bytes.size(), bytes.data(),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ITextureView* views[]{outputs[0]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                              outputs[1]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
        context->SetRenderTargets(2, views, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport viewport{0, 0, 4, 2, 0, 1};
        context->SetViewports(1, &viewport, 4, 2);
        context->SetPipelineState(pass);
        context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
        presentation.end_frame(context);
        const auto color = test_bloom::read(device, context, outputs[0]);
        const auto indirect = test_bloom::read(device, context, outputs[1]);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return std::array{color, indirect};
    };
    unsigned cases = 0;
    double worst = 0;
    for (float metal : {0.f, .5f, 1.f})
        for (float occlusion : {0.f, .25f, 1.f})
            for (float environment : {0.f, 2.f})
                for (bool offset : {false, true})
                    for (bool fog : {false, true}) {
                        material.parameters.at("metallic").value = {metal, 0, 0, 0};
                        material.parameters.at("occlusion").value = {occlusion, 0, 0, 0};
                        material.textures.at("lightmap").offset = {offset ? .5f : 0, 0};
                        constants.at("ForgeSurfaceMaterial") =
                            surface_parameter_bytes(material, layout);
                        const auto rows = surface_uv_rows(material, compiled.surface);
                        const auto uv_bytes = std::as_bytes(std::span(rows));
                        constants.at("ForgeSurfaceUV").assign(uv_bytes.begin(), uv_bytes.end());
                        const Row environment_value{environment, 1, 0, 1};
                        std::memcpy(constants.at("ForgeEnvironment").data(),
                                    environment_value.data(), sizeof(Row));
                        const std::array<Row, 2> fog_values{Row{.1f, .2f, .3f, fog ? 1.f : 0.f},
                                                            Row{0, 4, 0, 0}};
                        std::memcpy(constants.at("ForgeDistanceFog").data(), fog_values.data(),
                                    sizeof(fog_values));
                        const auto [color, indirect] = render();
                        for (unsigned y = 0; y < 2; ++y)
                            for (unsigned x = 0; x < 4; ++x) {
                                const auto& incoming = samples[offset || x >= 2 ? 1 : 0];
                                const double distance = std::sqrt(4 + std::pow((x + .5) * .01, 2) +
                                                                  std::pow((y + .5) * .01, 2));
                                const auto amount = fog ? distance / 4 : 0;
                                for (unsigned c = 0; c < 3; ++c) {
                                    const double response =
                                        incoming[c] *
                                        material.parameters.at("lightmap_gain").value[c] *
                                        material.parameters.at(base_key).value[c] * .96 *
                                        (1 - metal) * occlusion / std::numbers::pi;
                                    const double expected =
                                        (response + material.parameters.at("emissive").value[c]) *
                                            (1 - amount) +
                                        fog_values[0][c] * amount;
                                    const auto a = std::abs(color[y * 4 + x][c] - expected);
                                    const auto b =
                                        std::abs(indirect[y * 4 + x][c] - response * (1 - amount));
                                    worst = std::max({worst, a, b});
                                    check(a < 4e-6 && b < 4e-6,
                                          "Generated graph color/indirect/UV/fog response differs");
                                }
                                check(color[y * 4 + x][3] == 1 && indirect[y * 4 + x][3] == 0,
                                      "Generated graph changed color/indirect alpha ownership");
                            }
                        ++cases;
                    }
    // The shared irradiance helper owns validation after lighting preparation.
    // Poison only its reflected input to prove that deferred validation still
    // reports diagnostic color and keeps invalid values out of the indirect MRT.
    const auto gain_binding = std::find_if(layout.parameters.begin(), layout.parameters.end(),
                                           [](const auto& p) { return p.name == "lightmap_gain"; });
    check(gain_binding != layout.parameters.end() && gain_binding->width == 3,
          "Lightmap gain reflection disappeared");
    const Row dark_environment{0, 1, 0, 1};
    std::memcpy(constants.at("ForgeEnvironment").data(), dark_environment.data(),
                sizeof(dark_environment));
    std::fill(constants.at("ForgeDistanceFog").begin(), constants.at("ForgeDistanceFog").end(),
              std::byte{});
    material.parameters.at("emissive").value = {0, 0, 0, 0};
    for (const float poison :
         {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto values = surface_parameter_bytes(material, layout);
        const std::array<float, 3> invalid{poison, poison, poison};
        std::memcpy(values.data() + gain_binding->offset, invalid.data(), sizeof(invalid));
        constants.at("ForgeSurfaceMaterial") = std::move(values);
        const auto [color, indirect] = render();
        for (unsigned i = 0; i < color.size(); ++i)
            check(color[i] == Row{1, 0, 1, 1} && indirect[i] == Row{},
                  "Invalid graph irradiance escaped its shared diagnostic/MRT guard");
        ++cases;
    }
    // Two finite contributions can overflow only when combined. Exercise each
    // component alone before asserting the graph's combined MRT failure path.
    presentation.begin_frame();
    const Row high_cube{3.3e38f, 3.3e38f, 3.3e38f, 1};
    TextureSubResData high_level;
    high_level.pData = high_cube.data();
    high_level.Stride = sizeof(Row);
    for (unsigned face = 0; face < 6; ++face)
        context->UpdateTexture(cube, 0, face, Box{0, 1, 0, 1}, high_level,
                               RESOURCE_STATE_TRANSITION_MODE_NONE,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    presentation.end_frame(context);
    context->Flush();
    material.parameters.at(base_key).value = {1, 1, 1, 1};
    material.parameters.at("metallic").value = {0, 0, 0, 0};
    material.parameters.at("occlusion").value = {1, 0, 0, 0};
    std::fill(constants.at("ForgeDistanceFog").begin(), constants.at("ForgeDistanceFog").end(),
              std::byte{});
    for (unsigned component = 0; component < 3; ++component) {
        const float gain = component == 0 ? 0 : 4e37f;
        material.parameters.at("lightmap_gain").value = {gain, gain, gain, 0};
        constants.at("ForgeSurfaceMaterial") = surface_parameter_bytes(material, layout);
        const Row environment_value{component == 1 ? 0.f : 1.f, 1, 0, 1};
        std::memcpy(constants.at("ForgeEnvironment").data(), environment_value.data(), sizeof(Row));
        const auto [color, indirect] = render();
        for (unsigned i = 0; i < color.size(); ++i) {
            if (component == 2)
                check(color[i] == Row{1, 0, 1, 1} && indirect[i] == Row{},
                      "Combined finite irradiance overflow escaped into an indirect MRT");
            else
                check(color[i][0] > 1e37f && std::isfinite(color[i][0]) && indirect[i][0] > 1e37f &&
                          std::isfinite(indirect[i][0]),
                      "Overflow fixture did not isolate a finite lighting component");
        }
        ++cases;
    }
    std::cout << "Generated lightmap graph source: " << cases
              << " HDR/UV/gain/metal/occlusion/environment/emission/fog MRT cases, worst error "
              << worst << '\n';
}
} // namespace forge::lightmap_test
