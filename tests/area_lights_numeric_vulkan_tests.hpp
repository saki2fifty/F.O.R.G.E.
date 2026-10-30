#pragma once
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include <algorithm>
#include <numbers>
namespace forge::test_area_lights {
// Independent positive-integrand references: adaptive polar integration for a
// spherical cap and a source-plane antiderivative for rectangular emission.
// Neither reference uses the shader's boundary or fixed triangle/cap rules.
template <class F>
double integrate(const F& f, double lo, double hi, double a, double m, double b, double previous,
                 double tolerance, unsigned depth) {
    const double mid = (lo + hi) * .5, l = f((lo + mid) * .5), r = f((mid + hi) * .5);
    const double left = (mid - lo) * (a + 4 * l + m) / 6, right = (hi - mid) * (m + 4 * r + b) / 6;
    const double delta = left + right - previous;
    if (depth == 0 || std::abs(delta) < 15 * tolerance)
        return left + right + delta / 15;
    return integrate(f, lo, mid, a, l, m, left, tolerance * .5, depth - 1) +
           integrate(f, mid, hi, m, r, b, right, tolerance * .5, depth - 1);
}
template <class F> double integral(const F& f) {
    const double a = f(0), m = f(.5), b = f(1);
    const double tolerance = 1e-12 * std::max({1e-30, std::abs(a), std::abs(m), std::abs(b)});
    return integrate(f, 0, 1, a, m, b, (a + 4 * m + b) / 6, tolerance, 28);
}
struct Sample {
    std::array<float, 4> center, dimensions, normal;
};
inline double reference(const Sample& data) {
    constexpr double pi = std::numbers::pi;
    const double x = data.center[0], z = data.center[2], r = data.center[3],
                 distance2 = x * x + z * z, intensity = data.normal[3];
    const double normal_length = std::hypot(double(data.normal[0]), double(data.normal[2]));
    const double nx = data.normal[0] / normal_length, nz = data.normal[2] / normal_length;
    if (data.dimensions[2] == 5) {
        if (distance2 <= r * r || intensity == 0)
            return 0;
        const double sine = r / std::sqrt(distance2), c = (nx * x + nz * z) / std::sqrt(distance2),
                     sn = std::sqrt(std::max(0., 1 - c * c)),
                     gap = sine * sine / (1 + std::sqrt(1 - sine * sine));
        const auto polar = [&](double t) {
            const double latitude = 1 - gap * t, a = c * latitude,
                         b = sn * std::sqrt(gap * t * (2 - gap * t));
            if (a >= b)
                return 2 * pi * a;
            if (a <= -b)
                return 0.;
            return 2 * (a * std::acos(-a / b) + std::sqrt(std::max(0., b * b - a * a)));
        };
        return intensity * gap * integral(polar) / (pi * pi * r * r);
    }
    if (z <= 0 || intensity == 0)
        return 0;
    const double hx = data.dimensions[0] * .5, hy = data.dimensions[1] * .5;
    double lo = x - hx, hi = x + hx;
    if (nx > 0)
        lo = std::max(lo, -nz * z / nx);
    else if (nz <= 0)
        return 0;
    if (lo >= hi)
        return 0;
    const auto plane = [&](double t) {
        const double px = lo + (hi - lo) * t, a2 = px * px + z * z, a = std::sqrt(a2);
        const double along_y = hy / (a2 * (a2 + hy * hy)) + std::atan(hy / a) / (a2 * a);
        return z * std::max(0., nx * px + nz * z) * along_y;
    };
    return intensity * (hi - lo) * integral(plane) / (4 * hx * hy * pi);
}
inline void radiometry(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    std::vector<Sample> cases;
    const auto add = [&](std::array<float, 4> center, std::array<float, 4> dimensions,
                         float normal_z, float intensity = 1) {
        cases.push_back(
            {center,
             dimensions,
             {std::sqrt(std::max(0.f, 1 - normal_z * normal_z)), 0, normal_z, intensity}});
    };
    for (float sine : {1e-6f, .05f, .5f, .95f})
        for (float ratio : {-.99f, -.5f, 0.f, .5f, .99f}) {
            const float radius = std::max(.001f, sine), distance = radius / sine;
            add({0, 0, distance, radius}, {1, 1, 5, 0}, sine * ratio);
        }
    for (const auto& shape : std::array<std::array<float, 4>, 6>{{{0, .15f, 2, 1},
                                                                  {1, 2, 1, .4f},
                                                                  {0, 1, .001f, .001f},
                                                                  {1, 2, .001f, .001f},
                                                                  {0, 1000, .001f, .001f},
                                                                  {0, 1, 2, 1}}})
        for (float nz : {1.f, 0.f, -.05f})
            add({shape[0], 0, shape[1], 0}, {shape[2], shape[3], 4, 0}, nz);
    add({0, 0, .5f, 1}, {1, 1, 5, 0}, 1);
    add({0, 0, 0, 1}, {1, 1, 5, 0}, 1);
    add({0, 0, 0, 0}, {1, 1, 4, 0}, 1);
    add({0, 0, -1, 0}, {1, 1, 4, 0}, 1);
    add({0, 0, 1, .5f}, {1, 1, 5, 0}, 1, 0);
    add({0, 0, 1, 0}, {1, 1, 4, 0}, 1, 0);
    static_assert(sizeof(Sample) == 48);
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE area-light independent radiometry";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(float4 main(uint i:SV_VertexID):SV_Position {
        return float4(i==2?3:-1,i==1?3:-1,0,1);})";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation.shader(shader, &vertex);
    const auto program = std::string(R"(
#define ENABLE_CLEAR_COAT 0
#define ENABLE_SHEEN 0
#define ENABLE_ANISOTROPY 0
#define ENABLE_IRIDESCENCE 0
#define ENABLE_TRANSMISSION 0
#define ENABLE_SHADOWS 0
#define USE_IBL 0
#include "ForgeAreaLights.fxh"
)") + "cbuffer Samples {float4 Data[" +
                         std::to_string(cases.size() * 3) + R"(];};
float4 main(float4 position:SV_Position):SV_Target {
 uint i=(uint)position.x;float4 c=Data[i*3],d=Data[i*3+1],n=Data[i*3+2];
 SurfaceShadingInfo s=(SurfaceShadingInfo)0;s.BaseLayer.Normal=normalize(n.xyz);
 s.View=s.BaseLayer.Normal;s.BaseLayer.NdotV=1;s.BaseLayer.Srf.DiffuseColor=1;
 s.BaseLayer.Srf.PerceptualRoughness=.6;
 PBRLightAttribs light=(PBRLightAttribs)0;light.Type=(int)d.z;
 light.PosX=c.x;light.PosY=c.y;light.PosZ=c.z;
 light.IntensityR=.3*n.w;light.IntensityG=.7*n.w;light.IntensityB=n.w;
 SurfaceLightingInfo result=GetDefaultSurfaceLightingInfo();
 bool valid=ForgeApplyAreaLight(s,light,float3(c.w,d.xy),float3(1,0,0),float3(0,1,0),result);
 return float4(result.Base.Punctual,valid?1:0);
})";
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = program.c_str();
    shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    presentation.shader(shader, &pixel);
    BufferDesc desc;
    desc.Name = "FORGE independent emitter geometry cases";
    desc.Size = cases.size() * sizeof(Sample);
    desc.Usage = USAGE_IMMUTABLE;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    BufferData initial{cases.data(), desc.Size};
    RefCntAutoPtr<IBuffer> input;
    presentation.device()->CreateBuffer(desc, &initial, &input);
    check(bool(input), "Area radiometry input allocation failed");
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE area-light radiometry";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    info.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    info.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    info.GraphicsPipeline.NumRenderTargets = 1;
    info.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA32_FLOAT;
    info.pVS = vertex;
    info.pPS = pixel;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation.graphics(info, &pipeline);
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "Samples")->Set(input);
    TextureDesc target;
    target.Name = "FORGE full-precision emitter radiometry";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = unsigned(cases.size());
    target.Height = 1;
    target.Format = TEX_FORMAT_RGBA32_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> image;
    presentation.device()->CreateTexture(target, nullptr, &image);
    check(bool(image), "Area radiometry output allocation failed");
    auto* rtv = image->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const Viewport viewport{0, 0, float(cases.size()), 1, 0, 1};
    context->SetViewports(1, &viewport, target.Width, 1);
    context->SetPipelineState(pipeline);
    context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    const auto pixels = test_bloom::read(presentation.device(), context, image);
    double worst = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const auto& data = cases[i];
        const double expected = reference(data),
                     d2 = std::max(1e-6, double(data.center[0]) * data.center[0] +
                                             double(data.center[2]) * data.center[2]);
        const double horizon =
            data.dimensions[2] == 5
                ? data.center[3] / std::sqrt(d2)
                : std::min(1.,
                           std::max(std::abs(double(data.normal[0]) * data.center[0] +
                                             double(data.normal[2]) * data.center[2]),
                                    double(std::max(data.dimensions[0], data.dimensions[1])) * .5) /
                               std::sqrt(d2));
        const double budget =
            std::max(1e-30, (2e-6 * horizon + 1e-12) * std::max(1.f, data.normal[3]) / d2);
        check(pixels[i][3] == 1, "Area radiometry rejected valid source geometry");
        constexpr std::array<double, 3> channels{.3, .7, 1};
        for (unsigned c = 0; c < 3; ++c) {
            const double error = std::abs(pixels[i][c] - channels[c] * expected);
            worst = std::max(worst, error * d2);
            if (!std::isfinite(pixels[i][c]) || error > budget) {
                std::cerr << "Area radiometry case " << i << " channel " << c
                          << " expected=" << channels[c] * expected << " actual=" << pixels[i][c]
                          << " error=" << error << " budget=" << budget << '\n';
                throw std::runtime_error(
                    "Area diffuse lighting differs from independent physical integral");
            }
        }
    }
    std::cout << cases.size()
              << " independent native area diffuse/horizon/near/far/interior/side/zero cases PASS; "
              << "worst error times distance squared=" << worst << '\n';
}
} // namespace forge::test_area_lights
