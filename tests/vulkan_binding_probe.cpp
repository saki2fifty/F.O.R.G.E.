#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "material_shader.hpp"
#include "render_backend_tests.hpp"
#include "texture_gpu.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <forge/surface_shader.hpp>
#include <fstream>
#include <iostream>
namespace {
using namespace Diligent;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
forge::MaterialShader fixture() {
    forge::MaterialData source;
    source.model = "forge.gltf.metallic-roughness.v1";
    const auto layout = forge::prepare_pbr_material(source).layout;
    unsigned index = 0;
    for (const auto& [role, slot] : layout.textures) {
        auto& texture = source.textures[role];
        texture.semantic = slot.semantic;
        texture.sampler.min_lod = float(index++) / 32;
    }
    auto result =
        forge::material_shader(forge::prepare_pbr_material(source), std::array<unsigned, 1>{0});
    check(result.samplers.size() == 17, "Material fixture lost distinct sampler states");
    check(result.source.find("register(") == std::string::npos,
          "Shared material shader contains a native register declaration");
    return result;
}
void emit(const std::filesystem::path& directory, const forge::MaterialShader& material) {
    std::ofstream vertex(directory / "vertex.hlsl");
    vertex << "float4 main(uint id:SV_VertexID):SV_Position {"
              "return float4(id==2?3:-1,id==1?3:-1,0,1);}";
    check(bool(vertex.flush()), "Vertex source write failed");
    std::ofstream pixel(directory / "pixel.hlsl");
    pixel << material.source
          << "SamplerState environmentSampler; SamplerComparisonState shadowSampler;"
             "Texture2D<float4> environmentTexture; Texture2D<float> shadowTexture;"
             "float4 main(float4 position:SV_Position):SV_Target0 {"
             "float2 uv=position.xy/16;float4 value=0;bool valid;";
    for (const auto& texture : material.textures)
        pixel << "value+=ForgeSample_" << texture.role
              << "(uv,valid);if(!valid)return float4(1,0,1,1);";
    pixel << "value+=environmentTexture.Sample(environmentSampler,uv)*"
             "shadowTexture.SampleCmpLevelZero(shadowSampler,uv,.5);"
             "return float4(value.rgb/"
          << material.textures.size() + 1 << ",1);}";
    check(bool(pixel.flush()), "Pixel source write failed");
    // Compile the same generated material-surface interface for Vulkan, with
    // every logical texture dimension and more than sixteen independent samplers.
    // This is cook-mapping evidence, not support for loading DXBC on Vulkan.
    forge::SurfaceShaderDefinition surface;
    surface.uv_sets = {17, 0};
    surface.parameters["tint"] = {forge::MaterialParameterType::LinearColor4, {1, 1, 1, 1}};
    std::string function = "float4 Shade(ForgeSurfaceInput input){float4 value=0;\n";
    for (unsigned i = 0; i < 19; ++i) {
        const auto role = "texture" + std::to_string(i);
        auto& slot = surface.textures[role];
        slot.dimension = forge::TextureDimension(i % 5);
        if (i % 5 < 2)
            slot.uv_set = 17;
        const auto coordinate = i % 5 == 0   ? "float2(.25,.75)"
                                : i % 5 == 3 ? "float4(1,0,0,0)"
                                             : "float3(.25,.75,0)";
        function += "value+=ForgeSample_" + role + "(" + coordinate + ");\n";
    }
    function += "return value*ForgeParameter_tint()/19;}\n";
    std::filesystem::create_directories(directory / "engine");
    std::ofstream header(directory / "engine/forge.surface.hlsli");
    header << forge::surface_shader_header(surface);
    check(bool(header.flush()), "Surface header write failed");
    std::ofstream body(directory / "surface.function.hlsli");
    body << function;
    check(bool(body.flush()), "Surface body write failed");
    for (const bool depth : {false, true}) {
        std::ofstream wrapper(directory / (depth ? "surface.depth.hlsl" : "surface.color.hlsl"));
        wrapper << forge::surface_shader_wrapper("surface.function.hlsli", "Shade", depth);
        check(bool(wrapper.flush()), "Surface wrapper write failed");
    }
}
void draw(const std::filesystem::path& directory, const forge::MaterialShader& material) {
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo info;
    auto* factory = GetEngineFactoryVk();
    factory->CreateDeviceAndContextsVk(info, &device, &context);
    check(device && context, "Vulkan device creation failed");
    std::cout << "Vulkan adapter: " << device->GetAdapterInfo().Description << '\n';
    const auto sampler_limits = forge::sampler_backend_limits(device);
    check(sampler_limits.maximum_lod_bias > 0 &&
              sampler_limits.border == forge::SamplerBorderProfile::BlackOrWhite,
          "Native Vulkan sampler limits were not queried");
    const auto rejects = [&](auto fn) {
        bool rejected = false;
        try {
            fn();
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Unsupported Vulkan sampler was not rejected before creation");
    };
    forge::SamplerState sampler_state;
    sampler_state.lod_bias = sampler_limits.maximum_lod_bias;
    check(bool(forge::upload_sampler(device, sampler_state)), "Boundary LOD bias failed");
    sampler_state.lod_bias =
        std::nextafter(sampler_limits.maximum_lod_bias, std::numeric_limits<float>::infinity());
    rejects([&] { forge::upload_sampler(device, sampler_state); });
    sampler_state = {};
    sampler_state.u = forge::TextureWrap::ClampBorder;
    sampler_state.border = {.25f, .5f, .75f, 1};
    rejects([&] { forge::upload_sampler(device, sampler_state); });
    sampler_state.u = forge::TextureWrap::Repeat;
    check(bool(forge::upload_sampler(device, sampler_state)), "Unused border was rejected");
    check(sampler_state.border[0] == .25f, "GPU admission rewrote authored border state");
    std::cout << "Queried sampler bias range: " << sampler_limits.minimum_lod_bias << ".."
              << sampler_limits.maximum_lod_bias << "; invalid bias/border rejected; PASS\n";
    const auto shader = [&](const char* name, SHADER_TYPE stage) {
        std::ifstream input(directory / name, std::ios::binary);
        std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
        check(!bytes.empty() && bytes.size() % 4 == 0, "SPIR-V input is unavailable");
        ShaderCreateInfo ci;
        ci.Desc.Name = name;
        ci.Desc.ShaderType = stage;
        ci.EntryPoint = "main";
        ci.ByteCode = bytes.data();
        ci.ByteCodeSize = bytes.size();
        forge::prepare_renderer_shader(device->GetDeviceInfo(), ci);
        RefCntAutoPtr<IShader> result;
        device->CreateShader(ci, &result);
        check(bool(result), "Vulkan SPIR-V shader creation failed");
        return result;
    };
    auto vs = shader("vertex.spv", SHADER_TYPE_VERTEX);
    auto ps = shader("pixel.spv", SHADER_TYPE_PIXEL);
    GraphicsPipelineStateCreateInfo pipeline;
    pipeline.PSODesc.Name = "FORGE material binding portability probe";
    pipeline.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    pipeline.pVS = vs;
    pipeline.pPS = ps;
    auto& graphics = pipeline.GraphicsPipeline;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    graphics.RasterizerDesc.CullMode = CULL_MODE_NONE;
    graphics.DepthStencilDesc.DepthEnable = false;
    RefCntAutoPtr<IPipelineState> pso;
    device->CreateGraphicsPipelineState(pipeline, &pso);
    check(bool(pso), "Vulkan material pipeline creation failed");
    RefCntAutoPtr<IShaderResourceBinding> bindings;
    pso->CreateShaderResourceBinding(&bindings, true);
    check(bool(bindings), "Vulkan resource binding allocation failed");
    auto variable = [&](const char* name) {
        auto* result = bindings->GetVariableByName(SHADER_TYPE_PIXEL, name);
        check(result != nullptr, "Vulkan material binding is missing");
        return result;
    };
    std::vector<RefCntAutoPtr<ISampler>> samplers;
    std::vector<IDeviceObject*> sampler_objects;
    for (const auto& state : material.samplers) {
        auto sampler = forge::upload_sampler(device, state);
        check(bool(sampler), "Vulkan sampler creation failed");
        sampler_objects.push_back(sampler);
        samplers.push_back(std::move(sampler));
    }
    auto* array = variable(forge::material_sampler_variable);
    ShaderResourceDesc reflection;
    array->GetResourceDesc(reflection);
    check(reflection.ArraySize == sampler_objects.size(), "Sampler array reflection mismatch");
    array->SetArray(sampler_objects.data(), 0, unsigned(sampler_objects.size()));
    variable("environmentSampler")->Set(samplers.front());
    SamplerDesc comparison;
    comparison.MinFilter = comparison.MagFilter = FILTER_TYPE_COMPARISON_LINEAR;
    comparison.MipFilter = FILTER_TYPE_COMPARISON_POINT;
    comparison.ComparisonFunc = COMPARISON_FUNC_LESS;
    RefCntAutoPtr<ISampler> comparison_sampler;
    device->CreateSampler(comparison, &comparison_sampler);
    check(bool(comparison_sampler), "Comparison sampler creation failed");
    variable("shadowSampler")->Set(comparison_sampler);
    BufferDesc buffer;
    buffer.Name = "Material fixture values";
    buffer.Size = material.uniforms.size() * sizeof(material.uniforms.front());
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.Usage = USAGE_IMMUTABLE;
    BufferData values{material.uniforms.data(), buffer.Size};
    RefCntAutoPtr<IBuffer> uniforms;
    device->CreateBuffer(buffer, &values, &uniforms);
    check(bool(uniforms), "Material uniform allocation failed");
    variable("ForgeMaterialValues")->Set(uniforms);
    TextureDesc td;
    td.Name = "Material fixture texture";
    td.Type = RESOURCE_DIM_TEX_2D;
    td.Width = td.Height = 1;
    td.Format = TEX_FORMAT_RGBA8_UNORM;
    td.BindFlags = BIND_SHADER_RESOURCE;
    td.Usage = USAGE_IMMUTABLE;
    std::vector<RefCntAutoPtr<ITexture>> textures;
    unsigned sum = 0;
    for (unsigned i = 0; i < material.textures.size(); ++i) {
        const auto shade = static_cast<unsigned char>(8 * (i + 1));
        const std::array<unsigned char, 4> pixel{shade, shade, shade, 255};
        TextureSubResData pixels{pixel.data(), 4};
        TextureData initial{&pixels, 1};
        RefCntAutoPtr<ITexture> texture;
        device->CreateTexture(td, &initial, &texture);
        check(bool(texture), "Material texture allocation failed");
        variable(material.textures[i].texture_variable.c_str())
            ->Set(texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        textures.push_back(std::move(texture));
        sum += shade;
    }
    variable("environmentTexture")
        ->Set(textures.front()->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    sum += 8;
    td.Name = "Material fixture shadow";
    td.Format = TEX_FORMAT_D32_FLOAT;
    td.Usage = USAGE_DEFAULT;
    td.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> depth;
    device->CreateTexture(td, nullptr, &depth);
    check(bool(depth), "Depth allocation failed");
    context->ClearDepthStencil(depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), CLEAR_DEPTH_FLAG,
                               1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    variable("shadowTexture")->Set(depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    td.Name = "Material fixture output";
    td.Width = td.Height = 16;
    td.Format = TEX_FORMAT_RGBA8_UNORM;
    td.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> output;
    device->CreateTexture(td, nullptr, &output);
    check(bool(output), "Output allocation failed");
    auto* rtv = output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetPipelineState(pso);
    context->CommitShaderResources(bindings, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    td.Name = "Material fixture readback";
    td.Usage = USAGE_STAGING;
    td.BindFlags = BIND_NONE;
    td.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(td, nullptr, &staging);
    check(bool(staging), "Readback allocation failed");
    CopyTextureAttribs copy;
    copy.pSrcTexture = output;
    copy.pDstTexture = staging;
    copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode =
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    check(mapped.pData != nullptr, "Readback map failed");
    const auto* center =
        static_cast<const unsigned char*>(mapped.pData) + 8 * mapped.Stride + 8 * 4;
    const std::array<unsigned char, 4> pixel{center[0], center[1], center[2], center[3]};
    context->UnmapTextureSubresource(staging, 0, 0);
    const auto expected = std::lround(double(sum) / (material.textures.size() + 1));
    check(std::abs(int(pixel[0]) - expected) <= 1 && pixel[0] == pixel[1] && pixel[1] == pixel[2] &&
              pixel[3] == 255,
          "Actual material sampling did not produce the expected Vulkan pixel");
    std::cout << "FORGE generated material: " << material.samplers.size()
              << " independent samplers + environment + comparison, pixel=" << unsigned(pixel[0])
              << "; PASS\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Usage: forge_vulkan_binding_probe emit|draw directory");
        check_renderer_backend_policy();
        const auto material = fixture();
        if (std::string_view(argv[1]) == "emit")
            emit(argv[2], material);
        else if (std::string_view(argv[1]) == "draw")
            draw(argv[2], material);
        else
            throw std::runtime_error("Unknown probe operation");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
