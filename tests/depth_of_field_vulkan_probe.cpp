#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "depth_of_field.hpp"
#include "depth_of_field_scene_tests.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include <iostream>

using namespace Diligent;
using forge::test_bloom::check;
using Pixel = forge::test_bloom::Pixel;
namespace {
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Malformed native focus request admitted");
}
// Input geometry has independently known view-space distances, not decoded from
// the implementation's uploaded matrix. The top strip has no geometry.
double distance_at(unsigned x, unsigned y, unsigned width) {
    if (y < 3)
        return 0;
    return x < width / 3 ? 2 : x < 2 * width / 3 ? 10 : 20;
}
Pixel color_at(unsigned x, unsigned y, unsigned phase) {
    return {float((x + y + phase) % 2) * 2, float((x + phase) % 3) * .25f, float(y % 4) * .125f,
            ((x + y) % 2) ? .25f : .75f};
}
RefCntAutoPtr<ITexture> depth_texture(IRenderDevice* device, unsigned width, unsigned height) {
    TextureDesc desc;
    desc.Name = "FORGE focus oracle depth";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> result;
    device->CreateTexture(desc, nullptr, &result);
    check(bool(result), "Focus fixture depth allocation failed");
    return result;
}
class Producer {
  public:
    explicit Producer(forge::DiligentPresentation& presentation) {
        ShaderCreateInfo ci;
        ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ci.EntryPoint = "main";
        ci.Desc.Name = "FORGE focus oracle producer";
        ci.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ci.Source =
            R"(float4 main(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);})";
        RefCntAutoPtr<IShader> vertex, pixel;
        presentation.shader(ci, &vertex);
        ci.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ci.Source = R"(cbuffer Fixture {float4 Extent;float4 Projection;};
        struct Result {float4 color:SV_Target;float depth:SV_Depth;};
        Result main(float4 p:SV_Position) {
            uint x=uint(p.x),y=uint(p.y),width=uint(Extent.x),phase=uint(Extent.z);
            float z=x<width/3?2:x<2*width/3?10:20;
            Result r;r.color=float4(((x+y+phase)%2)*2,((x+phase)%3)*.25,(y%4)*.125,(x+y)%2?.25:.75);
            r.depth=y<3?1:(Projection.x*z+Projection.y)/(Projection.z*z+Projection.w);
            return r;
        })";
        presentation.shader(ci, &pixel);
        BufferDesc desc;
        desc.Name = "FORGE focus oracle parameters";
        desc.Size = 32;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        desc.Usage = USAGE_DYNAMIC;
        desc.CPUAccessFlags = CPU_ACCESS_WRITE;
        presentation.device()->CreateBuffer(desc, nullptr, &values_);
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE focus oracle geometry";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        auto& g = info.GraphicsPipeline;
        g.NumRenderTargets = 1;
        g.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
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
        check(values_ && binding_, "Focus fixture pipeline/buffer unavailable");
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, "Fixture")->Set(values_);
    }
    void draw(IDeviceContext* context, ITexture* color, ITexture* depth,
              const forge::CameraView& camera, unsigned phase) {
        const auto& desc = color->GetDesc();
        {
            MapHelper<float> data(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
            data[0] = float(desc.Width);
            data[1] = float(desc.Height);
            data[2] = float(phase);
            data[3] = 0;
            data[4] = camera.projection[10];
            data[5] = camera.projection[11];
            data[6] = camera.projection[14];
            data[7] = camera.projection[15];
        }
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{0, 0, float(desc.Width), float(desc.Height), 0, 1};
        context->SetViewports(1, &viewport, desc.Width, desc.Height);
        context->SetPipelineState(pipeline_);
        context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    }

  private:
    RefCntAutoPtr<IPipelineState> pipeline_;
    RefCntAutoPtr<IShaderResourceBinding> binding_;
    RefCntAutoPtr<IBuffer> values_;
};
std::vector<Pixel> oracle(unsigned width, unsigned height, forge::PixelViewport area,
                          forge::SceneDepthOfField settings, unsigned phase) {
    // Explicit geometric disk enumeration, independent of shader source/owners.
    constexpr std::array<std::array<int, 2>, 29> disk{
        {{{0, -3}}, {{-2, -2}}, {{-1, -2}}, {{0, -2}}, {{1, -2}}, {{2, -2}}, {{-2, -1}}, {{-1, -1}},
         {{0, -1}}, {{1, -1}},  {{2, -1}},  {{-3, 0}}, {{-2, 0}}, {{-1, 0}}, {{0, 0}},   {{1, 0}},
         {{2, 0}},  {{3, 0}},   {{-2, 1}},  {{-1, 1}}, {{0, 1}},  {{1, 1}},  {{2, 1}},   {{-2, 2}},
         {{-1, 2}}, {{0, 2}},   {{1, 2}},   {{2, 2}},  {{0, 3}}}};
    std::vector<Pixel> result(width * height);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            auto value = color_at(x, y, phase);
            const auto z = distance_at(x, y, width);
            if (settings.enabled && z && x >= area.x && y >= area.y && x < area.x + area.width &&
                y < area.y + area.height) {
                const double radius =
                    settings.blur_radius *
                    std::min(1., std::abs(z - settings.focus_distance) / settings.focus_range);
                if (radius >= .5) {
                    std::array<double, 3> sum{};
                    unsigned count = 0;
                    for (const auto& offset : disk) {
                        const auto tx = int(x) + int(std::nearbyint(offset[0] * radius / 3));
                        const auto ty = int(y) + int(std::nearbyint(offset[1] * radius / 3));
                        if (tx < int(area.x) || ty < int(area.y) ||
                            tx >= int(area.x + area.width) || ty >= int(area.y + area.height))
                            continue;
                        const auto other = distance_at(unsigned(tx), unsigned(ty), width);
                        if (!other ||
                            (other - settings.focus_distance) * (z - settings.focus_distance) < 0 ||
                            std::abs(other - z) > std::max(.01, .02 * z))
                            continue;
                        const auto sample = color_at(unsigned(tx), unsigned(ty), phase);
                        for (unsigned c = 0; c < 3; ++c)
                            sum[c] += sample[c];
                        ++count;
                    }
                    check(count != 0, "Focus reference disk lost its center");
                    for (unsigned c = 0; c < 3; ++c)
                        value[c] = float(sum[c] / count);
                }
            }
            result[y * width + x] = value;
        }
    return result;
}
void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    Producer producer(presentation);
    forge::DepthOfField effect(presentation, 128 * 1024);
    unsigned cases = 0;
    for (bool perspective : {false, true})
        for (bool infinite : {false, true}) {
            if (!perspective && infinite)
                continue;
            for (unsigned size : {0u, 1u})
                for (unsigned phase : {0u, 1u}) {
                    const unsigned width = size ? 47 : 65, height = size ? 31 : 49;
                    forge::Camera settings;
                    settings.projection = perspective ? 0u : 1u;
                    settings.near_plane = .1;
                    settings.far_plane = 200;
                    settings.infinite_far = infinite;
                    auto camera = forge::camera_view(settings, {}, width, height);
                    camera.viewport = phase ? forge::PixelViewport{7, 5, width - 11, height - 8}
                                            : forge::PixelViewport{0, 0, width, height};
                    std::vector<Pixel> blank(width * height);
                    auto color = forge::test_bloom::texture(device, width, height, blank);
                    auto depth = depth_texture(device, width, height);
                    for (float radius : {0.f, 5.25f, 12.f}) {
                        presentation.begin_frame();
                        producer.draw(context, color, depth, camera, phase);
                        forge::SceneDepthOfField focus{true, 10, 5, radius};
                        const auto allocations = effect.allocations();
                        effect.apply(context, color,
                                     depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), camera,
                                     focus);
                        if (radius == 0)
                            check(effect.allocations() == allocations,
                                  "Zero radius allocated focus scratch");
                        presentation.end_frame(context);
                        const auto observed = forge::test_bloom::read(device, context, color);
                        const auto expected = oracle(width, height, camera.viewport, focus, phase);
                        for (std::size_t i = 0; i < observed.size(); ++i)
                            for (unsigned c = 0; c < 4; ++c)
                                if (std::abs(observed[i][c] - expected[i][c]) > .0011f)
                                    throw std::runtime_error(
                                        "Focus pixel disagrees with independent disk/depth oracle "
                                        "at case " +
                                        std::to_string(cases) + " pixel " + std::to_string(i) +
                                        " lane " + std::to_string(c) + " observed " +
                                        std::to_string(observed[i][c]) + " expected " +
                                        std::to_string(expected[i][c]));
                        context->FinishFrame();
                        device->ReleaseStaleResources();
                        ++cases;
                    }
                    const auto payload = effect.payload_bytes();
                    auto bad = camera;
                    bad.viewport.width = std::numeric_limits<unsigned>::max();
                    rejects([&] {
                        effect.prepare(color, depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                       bad, {true});
                    });
                    check(effect.payload_bytes() == payload,
                          "Rejected growth lost previous focus targets");
                    forge::DepthOfField tiny(presentation, 16);
                    rejects([&] {
                        tiny.prepare(color, depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                     camera, {true});
                    });
                    rejects([&] { effect.prepare(color, nullptr, camera, {true}); });
                }
        }
    std::cout << cases
              << " native focus frames match independent HDR/depth disk oracle; "
                 "perspective/orthographic/infinite, clipping, changing input textures, "
                 "alpha/sky/focus/edges/resize/budget PASS\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Vulkan focus probe device unavailable");
        forge::DiligentPresentation presentation(device);
        exercise(presentation, context);
        check(argc == 3, "Expected isolated focus Scene fixture and Gallery directories");
        forge::test_focus_scene::exercise(presentation, context, argv[1]);
        forge::test_focus_scene::gallery(presentation, context, argv[2],
                                         std::filesystem::path(argv[1]) / "images");
        context->WaitForIdle();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
