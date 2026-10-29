#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "surface_reflections.hpp"
#include <iostream>
#include <thread>
using namespace Diligent;
using forge::test_bloom::check;
namespace {
RefCntAutoPtr<ITexture> target(IRenderDevice* device, unsigned w, unsigned h,
                               TEXTURE_FORMAT format) {
    TextureDesc d;
    d.Name = "FORGE reflected source test target";
    d.Type = RESOURCE_DIM_TEX_2D;
    d.Width = w;
    d.Height = h;
    d.Format = format;
    d.BindFlags = (format == TEX_FORMAT_D32_FLOAT ? BIND_DEPTH_STENCIL : BIND_RENDER_TARGET) |
                  BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> value;
    device->CreateTexture(d, nullptr, &value);
    check(bool(value), "Reflection test target unavailable");
    return value;
}
// The fixture owns test images; all reduction and lookup run through production.
struct SourceTargets {
    struct Level {
        RefCntAutoPtr<ITexture> color, range;
    };
    std::vector<Level> levels;
    void allocate(IRenderDevice* device, unsigned width, unsigned height) {
        levels.clear();
        const auto plan = forge::reflection_source_plan(width, height);
        for (unsigned i = 0; i < plan.count; ++i)
            levels.push_back({target(device, plan.levels[i].width, plan.levels[i].height,
                                     TEX_FORMAT_RGBA16_FLOAT),
                              target(device, plan.levels[i].width, plan.levels[i].height,
                                     TEX_FORMAT_RG32_FLOAT)});
    }
    std::vector<forge::SurfaceReflections::SourceLevel> views() const {
        std::vector<forge::SurfaceReflections::SourceLevel> result;
        for (const auto& level : levels)
            result.push_back({level.color, level.range});
        return result;
    }
};
} // namespace

// Compare every cell with sums over original source pixels. The oracle neither
// runs a CPU reduction chain nor uses the production shader's projection helper.
void reduction(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    forge::SurfaceReflections effect(presentation);
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE independent reduction depth";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source =
        R"(float4 main(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);})";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation.shader(shader, &vertex);
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Source = R"(cbuffer Geometry {float4 Extent;};
 float main(float4 p:SV_Position):SV_Depth {
  int2 at=int2(p.xy);return Extent.z!=0&&(at.x+3*at.y)%4==0?1:(1+float(at.x%7)*.25)/4;
 })";
    presentation.shader(shader, &pixel);
    BufferDesc b;
    b.Name = "FORGE reduction test geometry";
    b.Size = 16;
    b.BindFlags = BIND_UNIFORM_BUFFER;
    b.Usage = USAGE_DYNAMIC;
    b.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    device->CreateBuffer(b, nullptr, &values);
    check(bool(values), "Reduction geometry constants unavailable");
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE independent reduction depth target";
    info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    auto& g = info.GraphicsPipeline;
    g.DSVFormat = TEX_FORMAT_D32_FLOAT;
    g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    g.RasterizerDesc.CullMode = CULL_MODE_NONE;
    g.DepthStencilDesc.DepthEnable = true;
    g.DepthStencilDesc.DepthWriteEnable = true;
    g.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_ALWAYS;
    info.pVS = vertex;
    info.pPS = pixel;
    RefCntAutoPtr<IPipelineState> pipeline;
    presentation.graphics(info, &pipeline);
    check(bool(pipeline), "Reduction geometry pipeline unavailable");
    RefCntAutoPtr<IShaderResourceBinding> binding;
    pipeline->CreateShaderResourceBinding(&binding, true);
    check(bool(binding), "Reduction geometry binding unavailable");
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "Geometry")->Set(values);
    unsigned cases = 0;
    double worst = 0;
    for (const auto& [width, height] :
         {std::pair{3u, 1u}, std::pair{3u, 7u}, std::pair{5u, 3u}, std::pair{7u, 7u},
          std::pair{257u, 7u}, std::pair{129u, 97u}, std::pair{640u, 81u}, std::pair{1u, 257u}})
        for (unsigned pattern = 0; pattern < 3; ++pattern)
            for (bool masked : {false, true}) {
                std::vector<forge::test_bloom::Pixel> pixels(width * height);
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width; ++x) {
                        const float value = pattern == 0     ? 8.f
                                            : pattern == 1   ? x % 4 < 2 ? .25f : 4.25f
                                            : x + 1 == width ? 8.f
                                                             : 0.f;
                        pixels[y * width + x] = {value, value, value,
                                                 0}; // authored alpha is not geometry coverage
                    }
                auto color = forge::test_bloom::texture(device, width, height, pixels);
                auto depth = target(device, width, height, TEX_FORMAT_D32_FLOAT);
                SourceTargets sources;
                sources.allocate(device, width, height);
                forge::Camera c;
                c.projection = 1;
                c.orthographic_height = 4;
                c.near_plane = 1;
                c.far_plane = 5;
                auto camera = forge::camera_view(c, {}, width, height);
                presentation.begin_frame();
                {
                    MapHelper<float> v(context, values, MAP_WRITE, MAP_FLAG_DISCARD);
                    v[0] = float(width);
                    v[1] = float(height);
                    v[2] = masked ? 1.f : 0.f;
                    v[3] = 0;
                }
                context->SetRenderTargets(0, nullptr,
                                          depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                Diligent::Viewport viewport{0, 0, float(width), float(height), 0, 1};
                context->SetViewports(1, &viewport, width, height);
                context->SetPipelineState(pipeline);
                context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
                forge::SurfaceReflections::SurfaceInput input;
                input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                input.projection = camera.projection;
                const auto views = sources.views();
                effect.build_sources(context, input, views);
                presentation.end_frame(context);
                for (unsigned level = 0; level < sources.levels.size(); ++level) {
                    const auto& image = sources.levels[level];
                    const auto actual = forge::test_bloom::read(device, context, image.color);
                    auto desc = image.range->GetDesc();
                    desc.Usage = USAGE_STAGING;
                    desc.BindFlags = BIND_NONE;
                    desc.CPUAccessFlags = CPU_ACCESS_READ;
                    RefCntAutoPtr<ITexture> staging;
                    device->CreateTexture(desc, nullptr, &staging);
                    check(bool(staging), "Range staging unavailable");
                    context->CopyTexture({image.range, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                          staging, RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
                    context->WaitForIdle();
                    MappedTextureSubresource mapped;
                    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT,
                                                   nullptr, mapped);
                    check(mapped.pData != nullptr, "Range map unavailable");
                    std::vector<std::array<float, 2>> ranges(desc.Width * desc.Height);
                    for (unsigned y = 0; y < desc.Height; ++y)
                        std::memcpy(ranges.data() + y * desc.Width,
                                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                                    desc.Width * 8);
                    context->UnmapTextureSubresource(staging, 0, 0);
                    const unsigned scale = 1u << (level + 1);
                    for (unsigned y = 0; y < desc.Height; ++y)
                        for (unsigned x = 0; x < desc.Width; ++x) {
                            unsigned count = 0;
                            double sum = 0;
                            float low = 1e30f, high = 0;
                            for (unsigned sy = y * scale; sy < std::min(height, (y + 1) * scale);
                                 ++sy)
                                for (unsigned sx = x * scale; sx < std::min(width, (x + 1) * scale);
                                     ++sx) {
                                    if (masked && (sx + 3 * sy) % 4 == 0)
                                        continue;
                                    ++count;
                                    sum += pixels[sy * width + sx][0];
                                    const float z = 2 + float(sx % 7) * .25f;
                                    low = std::min(low, z);
                                    high = std::max(high, z);
                                }
                            const auto& value = actual[y * desc.Width + x];
                            const auto& range = ranges[y * desc.Width + x];
                            if (!count) {
                                check(value == forge::test_bloom::Pixel{} &&
                                          range == std::array<float, 2>{},
                                      "Invalid geometry retained source data");
                                continue;
                            }
                            const double error = std::abs(value[0] / value[3] - sum / count);
                            worst = std::max(worst, error);
                            check(std::isfinite(value[0]) && value[3] > 0 && error < .01,
                                  "Source cell mean differs from original pixels");
                            check(std::abs(value[3] - double(count) / (scale * scale)) < .0001,
                                  "Source cell coverage differs from original geometry");
                            check(range == std::array<float, 2>{low, high},
                                  "Source depth endpoints differ from valid original geometry");
                        }
                }
                context->FinishFrame();
                device->ReleaseStaleResources();
                ++cases;
            }
    std::cout << cases
              << " production source reductions PASS; every cell/original-pixel mean, coverage and "
                 "depth; max error="
              << worst << '\n';
}
void source_boundaries(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    forge::SurfaceReflections effect(presentation);
    SourceTargets targets;
    targets.allocate(device, 9, 7);
    auto views = targets.views();
    const std::vector<forge::test_bloom::Pixel> pixels(63, {.5, .25, .125, .75});
    auto color = forge::test_bloom::texture(device, 9, 7, pixels),
         out = forge::test_bloom::texture(device, 9, 7, pixels),
         light = forge::test_bloom::texture(device, 9, 7, pixels);
    auto depth = target(device, 9, 7, TEX_FORMAT_D32_FLOAT);
    forge::SurfaceReflections::Input input;
    input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.indirect = input.surface = input.response = input.specular = input.color;
    input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    input.projection = forge::camera_view(forge::Camera{}, {}, 9, 7).projection;
    input.settings = {true, 8, .1f, 1, .9f, true};
    input.sources = views;
    const auto misses = presentation.cache_misses();
    const auto reject = [&](auto action) {
        bool failed = false;
        try {
            action();
        } catch (const std::exception&) {
            failed = true;
        }
        check(failed, "Invalid reflection source group admitted");
    };
    reject([&] { effect.build_sources(nullptr, input, views); });
    {
        auto bad = input;
        bad.color = nullptr;
        reject([&] { effect.build_sources(context, bad, views); });
    }
    {
        auto bad = input;
        bad.depth = input.color;
        reject([&] { effect.build_sources(context, bad, views); });
    }
    {
        auto bad = input;
        bad.projection[5] = 0;
        reject([&] { effect.build_sources(context, bad, views); });
    }
    auto incomplete = views;
    incomplete.pop_back();
    reject([&] { effect.build_sources(context, input, incomplete); });
    auto excessive = views;
    excessive.push_back(views.back());
    reject([&] { effect.build_sources(context, input, excessive); });
    auto wrong = views;
    wrong[0].range = views[0].color;
    reject([&] { effect.build_sources(context, input, wrong); });
    wrong = views;
    wrong[0].color = nullptr;
    reject([&] { effect.build_sources(context, input, wrong); });
    wrong = views;
    wrong[0].color = views.back().color;
    reject([&] { effect.build_sources(context, input, wrong); });
    {
        auto bad = input;
        bad.sources = incomplete;
        reject([&] {
            effect.apply(context, bad, out->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                         light->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
        });
    }
    bool foreign = false;
    std::thread thread([&] {
        try {
            effect.build_sources(context, input, views);
        } catch (const std::invalid_argument&) {
            foreign = true;
        }
    });
    thread.join();
    check(foreign, "Reflection sources admitted a foreign thread");
    check(presentation.cache_misses() == misses, "Rejected source group prepared a pipeline");
    auto bypass = input;
    bypass.settings.max_distance = 0;
    bypass.sources = {};
    bypass.depth = bypass.surface = bypass.response = bypass.specular = nullptr;
    effect.apply(context, bypass, out->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                 light->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
    check(presentation.cache_misses() == misses &&
              forge::test_bloom::read(device, context, out) == pixels,
          "Filtered zero path required source processing");
    context->ClearDepthStencil(depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), CLEAR_DEPTH_FLAG,
                               1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    effect.build_sources(context, input, views);
    for (const auto& level : targets.levels)
        for (const auto& value : forge::test_bloom::read(device, context, level.color))
            check(value == forge::test_bloom::Pixel{}, "Valid retry retained invalid geometry");
    std::cout << "Production reflection source type/count/extent/projection/thread, pipeline-free "
                 "rejection and retry PASS\n";
}

class Producer {
  public:
    explicit Producer(forge::DiligentPresentation& presentation) {
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.Desc.Name = "FORGE independent reflection geometry";
        shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
            return float4(id==2?3:-1,id==1?3:-1,0,1);})";
        RefCntAutoPtr<IShader> vertex, pixel;
        presentation.shader(shader, &vertex);
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        // Analytic receiver z=2+2*x for x<=0, wall z=3 for x>0.
        // Their geometry and radiance do not use the reflection operator.
        shader.Source = R"(
cbuffer Fixture {float4 Extent;float4 P0;float4 P1;float4 P2;float4 P3;};
struct Result {float4 color:SV_Target0;float4 indirect:SV_Target1;
 float4 surface:SV_Target2;float4 response:SV_Target3;float4 specular:SV_Target4;float depth:SV_Depth;};
Result main(float4 p:SV_Position) {
 Result r=(Result)0;float ndc=p.x/Extent.x*2-1;
 float wallX=(ndc*(P3.z*3+P3.w)-P0.z*3-P0.w)/P0.x;bool receiver=wallX<=0;
 float wallZ=int(Extent.z)==4&&p.y>Extent.y*.5?2.6:3.;
 float z=receiver?(2+2*(ndc*P3.w-P0.w)/P0.x)/(1-2*(ndc*P3.z-P0.z)/P0.x):wallZ;
 float d=(P2.z*z+P2.w)/(P3.z*z+P3.w);
 r.depth=int(Extent.z)==5||(!receiver&&int(Extent.z)==6)||z<=.1||!(d>=0&&d<1)||(!receiver&&int(Extent.z)==3&&int(p.y)%6==0)?1:d;
 float3 wall=int(Extent.z)==1?float3(int(p.y)/2%2?4.25:.25,.4,.2):int(Extent.z)==2?float3(8,4,2):float3(.8,.4,.2);
 if(int(Extent.z)==4&&wallZ<3)wall=float3(.25,2,4);
 if(int(Extent.z)==8)wall=float3(2.25+2*sin(p.y*.196349540849362),.4,.2);
 if(int(Extent.z)==9)wall=65504.;
 r.color=float4(receiver?(int(Extent.z)==9?float3(60000,60000,60000):float3(.7,.5,.7)):wall,int(Extent.z)==7?0:(int(p.x)+int(p.y))%2?.25:.75);
 r.indirect=float4(receiver?(int(Extent.z)==9?float3(50000,50000,50000):float3(.2,.2,.2)):float3(.05,.05,.05),0);
 r.surface=receiver?float4(normalize(float3(2,0,-1)),Extent.w):0;
 r.response=receiver?float4(.5,.25,.75,0):0;r.specular=receiver?float4(.05,.05,.05,0):0;return r;
})";
        presentation.shader(shader, &pixel);
        BufferDesc desc;
        desc.Name = "FORGE reflection analytic camera";
        desc.Size = 80;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        desc.Usage = USAGE_DYNAMIC;
        desc.CPUAccessFlags = CPU_ACCESS_WRITE;
        presentation.device()->CreateBuffer(desc, nullptr, &values_);
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE analytic receiver and wall";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        auto& g = info.GraphicsPipeline;
        g.NumRenderTargets = 5;
        for (unsigned i = 0; i < 5; ++i)
            g.RTVFormats[i] = TEX_FORMAT_RGBA16_FLOAT;
        g.DSVFormat = TEX_FORMAT_D32_FLOAT;
        g.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        g.RasterizerDesc.CullMode = CULL_MODE_NONE;
        g.DepthStencilDesc.DepthEnable = true;
        g.DepthStencilDesc.DepthWriteEnable = true;
        g.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_ALWAYS;
        info.pVS = vertex;
        info.pPS = pixel;
        presentation.graphics(info, &pipeline_);
        pipeline_->CreateShaderResourceBinding(&binding_, true);
        check(values_ && binding_, "Reflection producer allocation failed");
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, "Fixture")->Set(values_);
    }
    void draw(IDeviceContext* context, const std::array<RefCntAutoPtr<ITexture>, 5>& targets,
              ITexture* depth, const forge::CameraView& camera, unsigned mode, float roughness) {
        const auto& size = targets[0]->GetDesc();
        {
            MapHelper<float> data(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
            data[0] = float(size.Width);
            data[1] = float(size.Height);
            data[2] = float(mode);
            data[3] = roughness;
            for (unsigned i = 0; i < 16; ++i)
                data[4 + i] = camera.projection[i];
        }
        ITextureView* views[5]{};
        for (unsigned i = 0; i < 5; ++i)
            views[i] = targets[i]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(5, views, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{0, 0, float(size.Width), float(size.Height), 0, 1};
        context->SetViewports(1, &viewport, size.Width, size.Height);
        context->SetPipelineState(pipeline_);
        context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    }

  private:
    RefCntAutoPtr<IPipelineState> pipeline_;
    RefCntAutoPtr<IShaderResourceBinding> binding_;
    RefCntAutoPtr<IBuffer> values_;
};

int main() {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Rough reflection device unavailable");
        forge::DiligentPresentation presentation(device);
        source_boundaries(presentation, context);
        reduction(presentation, context);
        Producer producer(presentation);
        SourceTargets pyramid;
        forge::SurfaceReflections filtered(presentation);
        forge::SurfaceReflections ordinary(presentation);
        unsigned cases = 0;
        std::size_t changed = 0, constant_exact = 0;
        std::uint64_t source_draws = 0;
        for (unsigned projection = 0; projection < 3; ++projection)
            for (bool larger : {false, true})
                for (bool jitter : {false, true}) {
                    const unsigned width = larger ? 129 : 97, height = larger ? 97 : 81;
                    const std::vector<forge::test_bloom::Pixel> blank(width * height);
                    std::array<RefCntAutoPtr<ITexture>, 5> targets;
                    for (auto& p : targets)
                        p = forge::test_bloom::texture(device, width, height, blank);
                    auto off = forge::test_bloom::texture(device, width, height, blank),
                         on = forge::test_bloom::texture(device, width, height, blank),
                         reference = forge::test_bloom::texture(device, width, height, blank);
                    auto ia = forge::test_bloom::texture(device, width, height, blank),
                         ib = forge::test_bloom::texture(device, width, height, blank),
                         ic = forge::test_bloom::texture(device, width, height, blank);
                    auto depth = target(device, width, height, TEX_FORMAT_D32_FLOAT);
                    forge::Camera settings;
                    settings.projection = projection ? 0u : 1u;
                    settings.infinite_far = projection == 2;
                    settings.orthographic_height = 6;
                    settings.near_plane = .1;
                    settings.far_plane = 100;
                    auto camera = forge::camera_view(settings, {}, width, height);
                    if (jitter)
                        for (unsigned c = 0; c < 4; ++c) {
                            camera.projection[c] += .3f / width * camera.projection[12 + c];
                            camera.projection[4 + c] -= .4f / height * camera.projection[12 + c];
                        }
                    std::vector<double> sweep_values;
                    auto run = [&](float roughness, unsigned pattern, bool sweep) {
                        presentation.begin_frame();
                        producer.draw(context, targets, depth, camera, pattern, roughness);
                        const auto draws_before = context->GetStats().CommandCounters.Draw;
                        pyramid.allocate(device, width, height);
                        forge::SurfaceReflections::SurfaceInput source;
                        source.color = targets[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        source.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        source.projection = camera.projection;
                        const auto views = pyramid.views();
                        filtered.build_sources(context, source, views);
                        const auto draws = context->GetStats().CommandCounters.Draw - draws_before;
                        check(draws == pyramid.levels.size(),
                              "Source native draw count differs from levels");
                        source_draws += draws;
                        forge::SurfaceReflections::Input input;
                        input.color = targets[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        input.indirect = targets[1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        input.surface = targets[2]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        input.response = targets[3]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        input.specular = targets[4]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                        input.projection = camera.projection;
                        input.settings = {true, 8, .1f, 1, .9f};
                        input.sources = views;
                        forge::SurfaceReflections::Input base;
                        base.color = input.color;
                        base.indirect = input.indirect;
                        base.surface = input.surface;
                        base.response = input.response;
                        base.specular = input.specular;
                        base.depth = input.depth;
                        base.projection = input.projection;
                        base.settings = input.settings;
                        ordinary.apply(context, base,
                                       reference->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                       ic->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                        filtered.apply(context, input,
                                       off->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                       ia->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                        input.settings.roughness_filtering = true;
                        filtered.apply(context, input,
                                       on->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                       ib->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                        presentation.end_frame(context);
                        const auto original = forge::test_bloom::read(device, context, targets[0]),
                                   a = forge::test_bloom::read(device, context, off),
                                   b = forge::test_bloom::read(device, context, on);
                        check(a == forge::test_bloom::read(device, context, reference),
                              "Default-off trace changed original point reflection");
                        const auto before = forge::test_bloom::read(device, context, targets[1]),
                                   after = forge::test_bloom::read(device, context, ib);
                        if (roughness == 0 && pattern != 9)
                            check(a == b, "Zero roughness changed sharp reflection");
                        if (pattern == 5 || pattern == 6)
                            check(b == original,
                                  "Empty geometry or missing source invented a reflection");
                        if (pattern == 5)
                            for (const auto& level : pyramid.levels)
                                for (const auto& value :
                                     forge::test_bloom::read(device, context, level.color))
                                    check(value == forge::test_bloom::Pixel{},
                                          "All-invalid depth retained source radiance/coverage");
                        if (pattern == 9)
                            check(std::any_of(b.begin(), b.end(),
                                              [](const auto& p) { return p[0] == 65504.f; }),
                                  "HDR fixture did not exercise finite output headroom");
                        std::size_t local = 0;
                        for (std::size_t p = 0; p < b.size(); ++p) {
                            check(b[p][3] == original[p][3] && after[p][3] == before[p][3],
                                  "Filtered trace changed color/indirect alpha");
                            for (unsigned c = 0; c < 3; ++c) {
                                // Independent half-storage ULP bound, not a filter calculation.
                                const float maximum =
                                    std::max({b[p][c], after[p][c], original[p][c], before[p][c]});
                                const float tolerance = std::max(
                                    .008f,
                                    std::ldexp(1.f, std::ilogb(std::max(maximum, 1.f)) - 10));
                                check(std::isfinite(b[p][c]) && b[p][c] >= 0 && b[p][c] <= 65504 &&
                                          std::isfinite(after[p][c]) && after[p][c] >= 0 &&
                                          after[p][c] <= 65504 &&
                                          std::abs((b[p][c] - original[p][c]) -
                                                   (after[p][c] - before[p][c])) <= tolerance,
                                      "Filtered trace lost finite HDR/indirect consistency");
                            }
                            local += b[p] != a[p];
                            constant_exact +=
                                pattern == 0 && a[p][0] > original[p][0] + .01 && a[p] == b[p];
                        }
                        if (projection == 0 && !jitter && pattern == 1 && roughness > 0) {
                            const unsigned x = width / 2 - 2;
                            const float previous =
                                forge::test_bloom::value(forge::test_bloom::half(.05f));
                            const float green =
                                forge::test_bloom::value(forge::test_bloom::half(.4f));
                            double mean = 0, variance = 0, sharp_mean = 0, sharp_variance = 0;
                            for (unsigned y = height / 2 - 8; y < height / 2 + 8; ++y) {
                                const auto at = y * width + x;
                                const double confidence =
                                    (b[at][1] - original[at][1]) / (green * .25 - previous);
                                const double sharp_confidence =
                                    (a[at][1] - original[at][1]) / (green * .25 - previous);
                                check(confidence > .01 && sharp_confidence > .01,
                                      "Analytic checker receiver has no trace confidence");
                                const double value =
                                    ((b[at][0] - original[at][0]) / confidence + previous) / .5;
                                const double sharp =
                                    ((a[at][0] - original[at][0]) / sharp_confidence + previous) /
                                    .5;
                                mean += value / 16;
                                variance += value * value / 16;
                                sharp_mean += sharp / 16;
                                sharp_variance += sharp * sharp / 16;
                            }
                            variance -= mean * mean;
                            sharp_variance -= sharp_mean * sharp_mean;
                            std::cout << "CHECKER " << width << ' ' << roughness << " mean=" << mean
                                      << " variance=" << variance
                                      << " sharp_variance=" << sharp_variance << '\n';
                            check(std::abs(mean - 2.25) < .06 && variance < .05 * sharp_variance &&
                                      sharp_variance > 1,
                                  "Filtered actual trace disagrees with analytic checker "
                                  "mean/variance");
                        }
                        if (projection == 0 && !jitter && pattern == 4 && roughness > 0) {
                            const unsigned x = width / 2 - 2;
                            for (unsigned y = height / 2; y <= height / 2 + 1; ++y) {
                                const auto at = y * width + x;
                                check(std::abs(a[at][0] - original[at][0]) > .01,
                                      "Depth-boundary oracle has no reflected source");
                                check(b[at] == a[at],
                                      "Rough footprint mixed distinct source-depth layers");
                            }
                            std::cout << "BOUNDARY " << width << ' ' << roughness
                                      << " two adjacent layers retain point response PASS\n";
                        }
                        if (sweep) {
                            const auto at = (height / 2 + 3) * width + width / 2 - 10;
                            const float previous =
                                forge::test_bloom::value(forge::test_bloom::half(.05f));
                            const float green =
                                forge::test_bloom::value(forge::test_bloom::half(.4f));
                            const double confidence =
                                (b[at][1] - original[at][1]) / (green * .25 - previous);
                            check(confidence > .01, "Sweep receiver has no reflection confidence");
                            const double value =
                                ((b[at][0] - original[at][0]) / confidence + previous) / .5;
                            check(std::isfinite(value) && value > .1 && value < 4.5,
                                  "Sweep radiance outside analytic signal bounds");
                            sweep_values.push_back(value);
                        }
                        changed += local;
                        std::cout << "TRACE " << projection << ' ' << width << ' ' << pattern << ' '
                                  << roughness << ' ' << local << '\n';
                        context->FinishFrame();
                        device->ReleaseStaleResources();
                        ++cases;
                    };
                    for (float roughness : {0.f, .3f, .6f})
                        for (unsigned pattern = 0; pattern < 8; ++pattern)
                            run(roughness, pattern, false);
                    run(.3f, 9, false);
                    if (projection == 0 && !larger && !jitter) {
                        for (unsigned i = 0; i <= 90; ++i)
                            run(.05f + .005f * i, 8, true);
                        double largest = 0;
                        for (std::size_t i = 1; i < sweep_values.size(); ++i)
                            largest =
                                std::max(largest, std::abs(sweep_values[i] - sweep_values[i - 1]));
                        std::cout << "SMOOTH " << sweep_values.size() << " max_step=" << largest
                                  << " first=" << sweep_values.front()
                                  << " last=" << sweep_values.back() << '\n';
                        check(largest < .2, "Roughness level boundary caused a radiance jump");
                        check(std::abs(sweep_values.back() - 2.25) <
                                  .5 * std::abs(sweep_values.front() - 2.25),
                              "Broad footprint failed to reduce sinusoidal detail");
                    }
                }
        context->WaitForIdle();
        check(changed > 100 && constant_exact > 100,
              "No filtered signal or exact constant region observed");
        std::cout << cases << " projected source/actual trace cases PASS; changed=" << changed
                  << " exact constant receivers=" << constant_exact
                  << " source_draws=" << source_draws << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
