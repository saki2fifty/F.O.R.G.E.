#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "shadow_lighting.hpp"
#include "shadow_renderer.hpp"
#include <algorithm>
#include <fstream>
#include <numbers>
#include <sstream>
using namespace Diligent;
using forge::test_bloom::check;
// Reuse the existing projection/cascade regression on the portable native host.
constexpr auto require = forge::test_bloom::check;
#include "shadow_view_tests.hpp"
namespace {
constexpr unsigned size = 128, width = 257;
struct Pass {
    RefCntAutoPtr<IPipelineState> pipeline;
    RefCntAutoPtr<IShaderResourceBinding> binding;
};
Pass pipeline(forge::DiligentPresentation& host, IShader* vertex, const std::string& source,
              bool depth) {
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Desc.Name = "FORGE original soft shadow prerequisite";
    shader.Source = source.c_str();
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    RefCntAutoPtr<IShader> pixel;
    host.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE soft shadow prerequisite";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    const ShaderResourceVariableDesc shadow_map{SHADER_TYPE_PIXEL, "g_ForgeShadows",
                                                SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
    if (!depth) {
        info.PSODesc.ResourceLayout.Variables = &shadow_map;
        info.PSODesc.ResourceLayout.NumVariables = 1;
    }
    auto& g = info.GraphicsPipeline;
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.DepthStencilDesc.DepthEnable = depth;
    g.DepthStencilDesc.DepthWriteEnable = depth;
    g.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_ALWAYS;
    if (depth)
        g.DSVFormat = TEX_FORMAT_D32_FLOAT;
    else {
        g.NumRenderTargets = 1;
        g.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
    }
    info.pVS = vertex;
    info.pPS = pixel;
    Pass pass;
    host.graphics(info, &pass.pipeline);
    check(bool(pass.pipeline), "Soft shadow production pipeline missing");
    pass.pipeline->CreateShaderResourceBinding(&pass.binding, true);
    check(bool(pass.binding), "Soft shadow production bindings missing");
    return pass;
}
void draw(IDeviceContext* context, const Pass& pass, ITextureView* color, ITextureView* depth,
          unsigned w, unsigned h) {
    context->SetRenderTargets(color ? 1 : 0, color ? &color : nullptr, depth,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::Viewport viewport{0, 0, float(w), float(h), 0, 1};
    context->SetViewports(1, &viewport, w, h);
    context->SetPipelineState(pass.pipeline);
    context->CommitShaderResources(pass.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
}
double segment(double t) {
    t = std::clamp(t, -1., 1.);
    return (std::acos(t) - t * std::sqrt(std::max(0., 1 - t * t))) / std::numbers::pi;
}
void point_consumer(forge::DiligentPresentation& host, IDeviceContext* context, IShader* vertex) {
    using namespace forge;
    const auto sphere = pipeline(host, vertex, R"(
cbuffer Sphere {float4 Right;float4 Up;float4 Forward;float4 Extent;};
float main(float4 p:SV_Position):SV_Depth {
    float2 ndc=(p.xy/Extent.w*2-1)*float2(1,-1);
    float3 ray=Right.xyz*(ndc.x/Right.w)+Up.xyz*(ndc.y/Up.w)+Forward.xyz;
    bool empty=Extent.z==1||(Extent.z==2&&dot(ray,float3(.2,.3,.9))<=0);
    return empty?1:Forward.w+Extent.x*length(ray)/Extent.y;
})",
                                 true);
    const auto receiver = pipeline(host, vertex, R"(
#include "ForgeShadows.fxh"
cbuffer Receiver {float4 Right;float4 Up;float4 Forward;};
float4 main(float4 p:SV_Position):SV_Target {
    float2 uv=float2(Right.w+((p.x/257)-.5)*.12,Up.w);
    float2 ndc=(uv*2-1)*float2(1,-1);
    float3 ray=normalize(Right.xyz*ndc.x+Up.xyz*ndc.y+Forward.xyz);
    float value=ForgeShadowVisibility(0,ray*Forward.w,float3(0,0,0),0);
    return float4(value,value,value,1);
})",
                                   false);
    BufferDesc buffer;
    buffer.Name = "FORGE point shadow analytic geometry";
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    const auto make_buffer = [&](unsigned bytes) {
        buffer.Size = bytes;
        RefCntAutoPtr<IBuffer> result;
        host.device()->CreateBuffer(buffer, nullptr, &result);
        check(bool(result), "Point shadow fixture buffer missing");
        return result;
    };
    const auto sphere_values = make_buffer(64), receiver_values = make_buffer(48),
               shadow_values = make_buffer(sizeof(ShadowLighting::values));
    sphere.binding->GetVariableByName(SHADER_TYPE_PIXEL, "Sphere")->Set(sphere_values);
    receiver.binding->GetVariableByName(SHADER_TYPE_PIXEL, "Receiver")->Set(receiver_values);
    receiver.binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeShadows")->Set(shadow_values);
    SamplerDesc sampler;
    sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
    sampler.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_BORDER;
    std::fill(std::begin(sampler.BorderColor), std::end(sampler.BorderColor), 1.f);
    RefCntAutoPtr<ISampler> comparison;
    host.device()->CreateSampler(sampler, &comparison);
    check(bool(comparison), "Point comparison sampler missing");
    receiver.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadowSampler")->Set(comparison);
    TextureDesc target;
    target.Name = "FORGE point shadow boundary visibility";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = width;
    target.Height = 1;
    target.Format = TEX_FORMAT_RGBA16_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color;
    host.device()->CreateTexture(target, nullptr, &color);
    check(bool(color), "Point shadow visibility image missing");
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.resolution = size;
    scene.settings.shadows.max_lights = 1;
    Light authored;
    authored.kind = std::uint32_t(LightKind::Point);
    authored.cast_shadows = true;
    authored.shadow_bias = authored.shadow_normal_bias = 0;
    authored.shadow_source_radius = .25f;
    authored.range = 20;
    const auto light = light_view(authored, {});
    const auto id = EntityId::generate();
    scene.lights = {{id, light}};
    const auto camera = camera_view(Camera{}, {}, width, 1);
    const auto views = punctual_shadow_views(light, scene.settings.shadows);
    check(views.size() == 6, "Point shadow source did not produce six real views");
    ShadowRenderer renderer(host);
    unsigned cases = 0, comparisons = 0;
    double maximum_jump = 0;
    for (unsigned pattern : {0u, 1u, 2u}) {
        host.begin_frame();
        unsigned draws = 0;
        renderer.render(context, scene, camera, UINT32_MAX, {},
                        [&](const CameraView& view, std::uint32_t layers) {
                            check(layers == UINT32_MAX, "Point shadow callback changed layers");
                            {
                                MapHelper<float> values(context, sphere_values, MAP_WRITE,
                                                        MAP_FLAG_DISCARD);
                                for (unsigned axis = 0; axis < 3; ++axis) {
                                    values[axis] = float(view.right[axis]);
                                    values[4 + axis] = float(view.up[axis]);
                                    values[8 + axis] = float(view.forward[axis]);
                                }
                                values[3] = view.projection[0];
                                values[7] = view.projection[5];
                                values[11] = view.projection[10];
                                values[12] = view.projection[11];
                                values[13] = 1; // Analytic blocker sphere's world radius.
                                values[14] = float(pattern);
                                values[15] = size;
                            }
                            context->SetPipelineState(sphere.pipeline);
                            context->CommitShaderResources(
                                sphere.binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                            context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
                            ++draws;
                        });
        check(draws == 6 && renderer.selection(id) == 0 && renderer.diagnostics().empty(),
              "Point shadow owner did not retain six valid source faces");
        const auto& lighting = renderer.lighting();
        {
            MapHelper<float> values(context, shadow_values, MAP_WRITE, MAP_FLAG_DISCARD);
            for (unsigned row = 0; row < lighting.values.size(); ++row)
                for (unsigned c = 0; c < 4; ++c)
                    values[row * 4 + c] = lighting.values[row][c];
        }
        std::array<IDeviceObject*, 8> maps;
        maps.fill(lighting.maps[0]);
        receiver.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadows")
            ->SetArray(maps.data(), 0, 8);
        for (const auto& view : views)
            for (float u : {.01f, .99f})
                for (float v : {0.f, .5f, 1.f}) {
                    {
                        MapHelper<float> values(context, receiver_values, MAP_WRITE,
                                                MAP_FLAG_DISCARD);
                        for (unsigned axis = 0; axis < 3; ++axis) {
                            values[axis] = float(view.camera.right[axis]);
                            values[4 + axis] = float(view.camera.up[axis]);
                            values[8 + axis] = float(view.camera.forward[axis]);
                        }
                        values[3] = u;
                        values[7] = v;
                        values[11] = 2;
                    }
                    draw(context, receiver, color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                         nullptr, width, 1);
                    context->Flush();
                    const auto pixels = forge::test_bloom::read(host.device(), context, color);
                    for (unsigned x = 0; x < width; ++x) {
                        const auto value = pixels[x][0];
                        check(std::isfinite(value) && value >= 0 && value <= 1,
                              "Point soft shadow emitted invalid visibility");
                        if (pattern < 2)
                            check(value == float(pattern),
                                  "Point face edge leaked or lost an empty map");
                        if (x && pattern == 2)
                            maximum_jump =
                                std::max(maximum_jump, std::abs(double(value - pixels[x - 1][0])));
                        const double at = u + ((x + .5) / width - .5) * .12;
                        Double3 ray;
                        double length = 0;
                        for (unsigned axis = 0; axis < 3; ++axis) {
                            ray[axis] = view.camera.right[axis] * (2 * at - 1) +
                                        view.camera.up[axis] * (1 - 2 * v) +
                                        view.camera.forward[axis];
                            length += ray[axis] * ray[axis];
                        }
                        const double side =
                            (ray[0] * .2 + ray[1] * .3 + ray[2] * .9) / std::sqrt(length);
                        if (pattern == 2 && std::abs(side) > .6) {
                            check(value == (side > 0 ? 0 : 1),
                                  "Point soft sampling lost world-space orientation");
                            ++comparisons;
                        }
                    }
                    ++cases;
                }
        host.end_frame(context);
        context->Flush();
    }
    check(comparisons > 1000 && maximum_jump < .15,
          "Point soft shadow has an unbounded face transition");
    std::cout << cases << " actual point-owner edge/corner strips; " << comparisons
              << " independent hemisphere comparisons; max adjacent jump " << maximum_jump
              << " <0.15; no full-map white-border leakage PASS\n";
}

void receiver_plane_consumer(forge::DiligentPresentation& host, IDeviceContext* context,
                             IShader* vertex) {
    using namespace forge;
    const auto caster = pipeline(host, vertex, R"(
cbuffer PlaneView {float4 Right;float4 Up;float4 Forward;float4 Origin;float4 Clip;float4 Plane;float4 Case;};
float main(float4 p:SV_Position):SV_Depth {
    float2 ndc=(p.xy/Clip.w*2-1)*float2(1,-1);
    bool perspective=Clip.z==0;
    float3 origin=Origin.xyz, ray=Forward.xyz;
    if(perspective)ray+=Right.xyz*(ndc.x/Right.w)+Up.xyz*(ndc.y/Up.w);
    else origin+=Right.xyz*((ndc.x-Clip.x)/Right.w)+Up.xyz*((ndc.y-Clip.y)/Up.w);
    float denominator=dot(Plane.xyz,ray);
    float t=(Plane.w-dot(Plane.xyz,origin))/denominator;
    float d=perspective?Forward.w+Origin.w/t:Forward.w*t+Origin.w;
    if(Case.x!=0&&abs(ray.z)>1e-6) {
        float blocker=(3-origin.z)/ray.z;
        float3 hit=origin+ray*blocker;
        float depth=perspective?Forward.w+Origin.w/blocker:Forward.w*blocker+Origin.w;
        if(hit.x<0&&(!perspective||blocker>0)&&depth>=0&&depth<=1)d=min(d,depth);
    }
    return isfinite(d)&&d>=0&&d<=1&&(!perspective||t>0)?d:1;
})",
                                 true);
    const auto receiver = pipeline(host, vertex, R"(
#include "ForgeShadows.fxh"
float4 main(float4 p:SV_Position):SV_Target {
    float2 xy=float2((p.x/129*2-1),(1-p.y/17*2));
    float3 world=float3(xy,5+.4*xy.x+.5*xy.y);
    float value=ForgeShadowVisibility(0,world,normalize(float3(.4,.5,-1)),world.z);
    return float4(value,value,value,1);
})",
                                   false);
    BufferDesc buffer;
    buffer.Name = "FORGE receiver plane fixture values";
    buffer.BindFlags = BIND_UNIFORM_BUFFER;
    buffer.Usage = USAGE_DYNAMIC;
    buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
    const auto make_buffer = [&](unsigned bytes) {
        buffer.Size = bytes;
        RefCntAutoPtr<IBuffer> result;
        host.device()->CreateBuffer(buffer, nullptr, &result);
        check(bool(result), "Receiver plane uniform buffer missing");
        return result;
    };
    const auto view_values = make_buffer(7 * 16),
               shadow_values = make_buffer(sizeof(ShadowLighting::values));
    caster.binding->GetVariableByName(SHADER_TYPE_PIXEL, "PlaneView")->Set(view_values);
    receiver.binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeShadows")->Set(shadow_values);
    SamplerDesc sampler;
    sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
    sampler.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_BORDER;
    std::fill(std::begin(sampler.BorderColor), std::end(sampler.BorderColor), 1.f);
    RefCntAutoPtr<ISampler> comparison;
    host.device()->CreateSampler(sampler, &comparison);
    check(bool(comparison), "Receiver plane comparison sampler missing");
    receiver.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadowSampler")->Set(comparison);
    TextureDesc target;
    target.Name = "FORGE receiver plane visibility";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = 129;
    target.Height = 17;
    target.Format = TEX_FORMAT_RGBA16_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color;
    host.device()->CreateTexture(target, nullptr, &color);
    check(bool(color), "Receiver plane visibility target missing");
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.resolution = 128;
    scene.settings.shadows.cascades = 1;
    scene.settings.shadows.max_lights = 1;
    scene.settings.shadows.distance = 10;
    Camera lens;
    lens.aspect = 1;
    lens.near_plane = .1;
    lens.far_plane = 10;
    const auto camera = camera_view(lens, {}, 129, 17);
    ShadowRenderer shadows(host);
    const auto id = EntityId::generate();
    unsigned cases = 0, exact_lit = 0;
    for (unsigned kind : {0u, 1u, 2u})
        for (unsigned pattern : {0u, 1u}) {
            Light light;
            light.kind = kind;
            light.cast_shadows = true;
            light.range = 20;
            light.shadow_source_radius = .5f;
            light.shadow_angular_radius = .2f;
            light.shadow_bias = .00001f;
            light.shadow_normal_bias = 0;
            scene.lights = {{id, light_view(light, {})}};
            host.begin_frame();
            shadows.render(
                context, scene, camera, UINT32_MAX, {}, [&](const CameraView& view, std::uint32_t) {
                    {
                        MapHelper<float> values(context, view_values, MAP_WRITE, MAP_FLAG_DISCARD);
                        for (unsigned axis = 0; axis < 3; ++axis) {
                            values[axis] = float(view.right[axis]);
                            values[4 + axis] = float(view.up[axis]);
                            values[8 + axis] = float(view.forward[axis]);
                            values[12 + axis] = float(view.position[axis]);
                        }
                        values[3] = view.projection[0];
                        values[7] = view.projection[5];
                        values[11] = view.projection[10];
                        values[15] = view.projection[11];
                        values[16] = view.projection[3];
                        values[17] = view.projection[7];
                        values[18] = view.projection[15];
                        values[19] = 128;
                        values[20] = .4f;
                        values[21] = .5f;
                        values[22] = -1;
                        values[23] = -5;
                        values[24] = float(pattern);
                        values[25] = values[26] = values[27] = 0;
                    }
                    context->SetPipelineState(caster.pipeline);
                    context->CommitShaderResources(caster.binding,
                                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
                });
            check(shadows.selection(id) == 0 && shadows.diagnostics().empty(),
                  "Plane shadow view unavailable");
            {
                MapHelper<float> values(context, shadow_values, MAP_WRITE, MAP_FLAG_DISCARD);
                for (unsigned row = 0; row < shadows.lighting().values.size(); ++row)
                    for (unsigned c = 0; c < 4; ++c)
                        values[row * 4 + c] = shadows.lighting().values[row][c];
            }
            std::array<IDeviceObject*, 8> maps;
            maps.fill(shadows.lighting().maps[0]);
            receiver.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadows")
                ->SetArray(maps.data(), 0, 8);
            draw(context, receiver, color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), nullptr, 129,
                 17);
            host.end_frame(context);
            context->Flush();
            const auto pixels = forge::test_bloom::read(host.device(), context, color);
            float minimum = 1;
            for (unsigned y = 0; y < 17; ++y)
                for (unsigned x = 0; x < 129; ++x) {
                    const auto value = pixels[y * 129 + x][0];
                    check(std::isfinite(value) && value >= 0 && value <= 1,
                          "Receiver plane visibility invalid");
                    minimum = std::min(minimum, value);
                    if (!pattern || x > 118) {
                        if (value != 1)
                            throw std::runtime_error(
                                "Receiver plane kind " + std::to_string(kind) + " pattern " +
                                std::to_string(pattern) + " pixel " + std::to_string(x) + "," +
                                std::to_string(y) + " visibility " + std::to_string(value));
                        ++exact_lit;
                    }
                }
            check(!pattern || minimum < .8f, "Independent half-plane blocker disappeared");
            ++cases;
        }
    std::cout << cases << " actual directional/point/spot sloped receiver cases; " << exact_lit
              << " independent self/far-lit pixels exact; separate blocker retained PASS\n";
}

} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--receiver-plane-only"),
              "Expected optional receiver-plane-only profile");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Soft shadow software device missing");
        forge::DiligentPresentation host(device);
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shader.Desc.Name = "FORGE soft shadow full triangle";
        shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position{
            return float4(id==2?3:-1,id==1?3:-1,0,1);})";
        RefCntAutoPtr<IShader> vertex;
        host.shader(shader, &vertex);
        if (argc == 2) {
            receiver_plane_consumer(host, context, vertex);
            context->WaitForIdle();
            return 0;
        }
        check_shadow_views(host);
        std::cout << "Existing large-origin/cascade/orthographic/punctual shadow views PASS\n";
        const std::string uniforms =
            R"(cbuffer Input{float4 Depth;float4 Projection;float4 Case;};)";
        const auto depth_pass = pipeline(host, vertex, uniforms + R"(
            float main(float4 p:SV_Position):SV_Depth{
                return Case.x==1?1:Case.x==2?Depth.y:Case.x==3?
                    (all(int2(p.xy)==int2(64,64))?Depth.y:1):p.x<64?Depth.y:1;
            })",
                                         true);
        const auto sample_pass =
            pipeline(host, vertex, uniforms + "\n#include \"ForgeShadows.fxh\"\n" + R"(
            float4 main(float4 p:SV_Position):SV_Target{
                float2 uv=float2(.5+((p.x/257)-.5)*.3,.5);
                float value=ForgeShadowSlice(0,0,float3((uv*2-1)*float2(1,-1),0));
                float fixed=FilterShadowMapFixedPCF(g_ForgeShadows[0],g_ForgeShadowSampler,
                    float4(Depth.w,Depth.w,1/Depth.w,1/Depth.w),uv,0,Depth.x-Case.y,float2(0,0));
                return float4(value,fixed,0,1);
            })",
                     false);
        BufferDesc buffer;
        buffer.Name = "FORGE soft shadow prerequisite inputs";
        buffer.Size = 3 * 16;
        buffer.BindFlags = BIND_UNIFORM_BUFFER;
        buffer.Usage = USAGE_DYNAMIC;
        buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
        RefCntAutoPtr<IBuffer> input;
        device->CreateBuffer(buffer, nullptr, &input);
        check(bool(input), "Soft shadow fixture buffer missing");
        for (const auto* pass : {&depth_pass, &sample_pass})
            pass->binding->GetVariableByName(SHADER_TYPE_PIXEL, "Input")->Set(input);
        TextureDesc desc;
        desc.Name = "FORGE soft shadow actual depth map";
        desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
        desc.Width = desc.Height = size;
        desc.ArraySize = 1;
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> depth, color;
        device->CreateTexture(desc, nullptr, &depth);
        desc.Name = "FORGE soft shadow numeric visibility";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = 1;
        desc.Format = TEX_FORMAT_RGBA16_FLOAT;
        desc.BindFlags = BIND_RENDER_TARGET;
        device->CreateTexture(desc, nullptr, &color);
        check(depth && color, "Soft shadow fixture images missing");
        std::array<IDeviceObject*, 8> maps;
        maps.fill(depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        sample_pass.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadows")
            ->SetArray(maps.data(), 0, 8);
        buffer.Size = sizeof(forge::ShadowLighting::values);
        RefCntAutoPtr<IBuffer> shadow_values;
        device->CreateBuffer(buffer, nullptr, &shadow_values);
        check(bool(shadow_values), "Shadow receiver fixture buffer missing");
        sample_pass.binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeShadows")
            ->Set(shadow_values);
        SamplerDesc sampler;
        sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_COMPARISON_LINEAR;
        sampler.ComparisonFunc = COMPARISON_FUNC_LESS_EQUAL;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_BORDER;
        std::fill(std::begin(sampler.BorderColor), std::end(sampler.BorderColor), 1.f);
        RefCntAutoPtr<ISampler> comparison;
        device->CreateSampler(sampler, &comparison);
        check(bool(comparison), "Comparison sampler missing");
        sample_pass.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_ForgeShadowSampler")
            ->Set(comparison);
        unsigned cases = 0, references = 0;
        double worst = 0;
        std::array<double, 3> previous_width{};
        for (bool perspective : {false, true})
            for (double receiver : {2., 4., 8.})
                for (unsigned pattern : {0u, 1u, 2u, 3u, 4u, 5u}) {
                    constexpr double near = .1, far = 20, blocker = 1;
                    const double a = far / (far - near), b = -far * near / (far - near);
                    const double ndc_r = perspective ? a + b / receiver : receiver / far;
                    const double ndc_b = perspective ? a + b / blocker : blocker / far;
                    const double source = perspective ? .12 : .5;
                    const double filter = perspective
                                              ? source * (receiver - blocker) / (receiver * blocker)
                                              : source * (receiver - blocker) / far;
                    host.begin_frame();
                    {
                        MapHelper<float> values(context, input, MAP_WRITE, MAP_FLAG_DISCARD);
                        const std::array<float, 12> data{float(ndc_r),
                                                         float(ndc_b),
                                                         float(perspective),
                                                         float(size),
                                                         float(a),
                                                         float(b),
                                                         float(source),
                                                         float(source),
                                                         float(pattern == 5   ? 3
                                                               : pattern >= 3 ? 0
                                                                              : pattern),
                                                         0,
                                                         0,
                                                         0};
                        for (unsigned i = 0; i < data.size(); ++i)
                            values[i] = data[i];
                    }
                    {
                        forge::ShadowLighting lighting;
                        auto& rows = lighting.values;
                        rows[0] = {float(perspective ? receiver : 1), 0, 0, 0};
                        rows[1] = {0, float(perspective ? receiver : 1), 0, 0};
                        rows[2] = {0, 0, 0, float(ndc_r * (perspective ? receiver : 1))};
                        rows[3] = {0, 0, 0, float(perspective ? receiver : 1)};
                        rows[40] = {1, 0, 0, perspective ? 2.f : 0.f};
                        rows[41][3] = size;
                        if (perspective) {
                            rows[32][2] = float(a);
                            rows[32][3] = float(b);
                            rows[39][2] = rows[39][3] = pattern == 3   ? 0.f
                                                        : pattern == 4 ? 1e-8f
                                                                       : float(source);
                        } else
                            rows[32][2] = rows[32][3] = pattern == 3   ? 0.f
                                                        : pattern == 4 ? 1e-8f
                                                                       : float(source);
                        MapHelper<float> values(context, shadow_values, MAP_WRITE,
                                                MAP_FLAG_DISCARD);
                        for (unsigned i = 0; i < rows.size(); ++i)
                            for (unsigned j = 0; j < 4; ++j)
                                values[i * 4 + j] = rows[i][j];
                    }
                    draw(context, depth_pass, nullptr,
                         depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), size, size);
                    draw(context, sample_pass, color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                         nullptr, width, 1);
                    host.end_frame(context);
                    context->Flush();
                    const auto pixels = forge::test_bloom::read(device, context, color);
                    for (unsigned x = 0; x < width; ++x) {
                        check(std::all_of(pixels[x].begin(), pixels[x].end(),
                                          [](float v) { return std::isfinite(v); }),
                              "Soft shadow produced nonfinite values");
                        check(pixels[x][0] >= 0 && pixels[x][0] <= 1, "Visibility outside bounds");
                        if (pattern == 1)
                            check(pixels[x][0] == 1, "Missing blockers darkened receiver");
                        if (pattern == 2)
                            check(pixels[x][0] == 0, "Full blocker failed");
                        if (pattern == 3 || pattern == 4)
                            check(pixels[x][0] == pixels[x][1],
                                  "Zero/sub-texel source extent changed fixed PCF");
                        if (pattern == 5 && x == width / 2)
                            check(pixels[x][0] < 1, "Centred single-texel blocker disappeared");
                        if (pattern == 0) {
                            const double uv = .5 + ((x + .5) / width - .5) * .3;
                            const double expected = segment((.5 - uv) / filter);
                            worst = std::max(worst, std::abs(pixels[x][0] - expected));
                            ++references;
                        }
                    }
                    if (pattern == 0) {
                        const auto actual =
                            std::count_if(pixels.begin(), pixels.end(),
                                          [](const auto& p) { return p[0] > .05f && p[0] < .95f; });
                        auto& prior = previous_width[perspective ? 1 : 0];
                        check(actual > prior, "Penumbra failed to grow with blocker separation");
                        prior = actual;
                    }
                    ++cases;
                }
        check(worst < .1, "Native disk visibility differs from continuous area reference");
        point_consumer(host, context, vertex);
        receiver_plane_consumer(host, context, vertex);
        context->WaitForIdle();
        std::cout << cases << " actual D32 blocker/search/filter cases; " << references
                  << " independent continuous-area comparisons; worst error " << worst
                  << " <0.1; production contact-hardening widths, empty/full blockers, exact "
                     "default PCF and finite bounds PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
