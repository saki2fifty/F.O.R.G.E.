#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "motion_blur.hpp"
#include "motion_blur_gallery_tests.hpp"
#include "motion_blur_scene_tests.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/render_view.hpp>
#include <iostream>

using namespace Diligent;
using forge::test_bloom::check;
namespace {
class Producer {
  public:
    explicit Producer(forge::DiligentPresentation& presentation) {
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.Desc.Name = "FORGE independent motion fixture";
        shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
            return float4(id==2?3:-1,id==1?3:-1,0,1);})";
        RefCntAutoPtr<IShader> vertex, pixel;
        presentation.shader(shader, &vertex);
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        shader.Source = R"(cbuffer Fixture {float4 Extent;float4 Velocity;float4 Projection;};
        struct Result {float4 color:SV_Target0;float4 motion:SV_Target1;float depth:SV_Depth;};
        Result main(float4 p:SV_Position) {
            int2 at=int2(p.xy);int mode=int(Extent.z);
            bool right=at.x>=int(Extent.x)/2;
            float z=(mode==1&&right)?20:10;
            float d=(Projection.x*z+Projection.y)/(Projection.z*z+Projection.w);
            float2 movement=(mode==2&&right)?-Velocity.xy:Velocity.xy;
            Result r;
            r.color=float4(float(at.x)/64.,float(at.y)/32.,float(at.x%2)*.5,at.x%2?.25:.75);
            if(mode==1||mode==2)r.color=float4(right?float3(0,1,0):float3(1,0,0),.75);
            r.motion=float4(movement*float2(2,-2)/Extent.xy,d,Velocity.z);
            r.depth=(mode==3||at.y<2)?1:d;
            return r;
        })";
        presentation.shader(shader, &pixel);
        BufferDesc desc;
        desc.Name = "FORGE motion fixture parameters";
        desc.Size = 48;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        desc.Usage = USAGE_DYNAMIC;
        desc.CPUAccessFlags = CPU_ACCESS_WRITE;
        presentation.device()->CreateBuffer(desc, nullptr, &values_);
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE motion fixture MRT";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        auto& g = info.GraphicsPipeline;
        g.NumRenderTargets = 2;
        g.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
        g.RTVFormats[1] = TEX_FORMAT_RGBA32_FLOAT;
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
        check(values_ && binding_, "Motion fixture buffer/pipeline allocation failed");
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, "Fixture")->Set(values_);
    }
    void draw(IDeviceContext* context, ITexture* color, ITexture* motion, ITexture* depth,
              const forge::CameraView& camera, float vx, float vy, unsigned mode, float validity) {
        const auto& d = color->GetDesc();
        {
            MapHelper<float> values(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
            values[0] = float(d.Width);
            values[1] = float(d.Height);
            values[2] = float(mode);
            values[3] = 0;
            values[4] = vx;
            values[5] = vy;
            values[6] = validity;
            values[7] = 0;
            values[8] = camera.projection[10];
            values[9] = camera.projection[11];
            values[10] = camera.projection[14];
            values[11] = camera.projection[15];
        }
        ITextureView* targets[]{color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                motion->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
        context->SetRenderTargets(2, targets, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport{0, 0, float(d.Width), float(d.Height), 0, 1};
        context->SetViewports(1, &viewport, d.Width, d.Height);
        context->SetPipelineState(pipeline_);
        context->CommitShaderResources(binding_, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    }

  private:
    RefCntAutoPtr<IPipelineState> pipeline_;
    RefCntAutoPtr<IShaderResourceBinding> binding_;
    RefCntAutoPtr<IBuffer> values_;
};
void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    Producer producer(presentation);
    forge::MotionBlur effect(presentation);
    unsigned cases = 0;
    for (bool perspective : {false, true})
        for (unsigned size : {0u, 1u}) {
            const unsigned width = size ? 73 : 65, height = size ? 37 : 33;
            std::vector<forge::test_bloom::Pixel> blank(width * height);
            auto color = forge::test_bloom::texture(device, width, height, blank);
            auto output = forge::test_bloom::texture(device, width, height, blank);
            auto create = [&](TEXTURE_FORMAT format, BIND_FLAGS flags) {
                TextureDesc d;
                d.Name = "FORGE motion fixture field";
                d.Type = RESOURCE_DIM_TEX_2D;
                d.Width = width;
                d.Height = height;
                d.Format = format;
                d.BindFlags = flags;
                RefCntAutoPtr<ITexture> result;
                device->CreateTexture(d, nullptr, &result);
                check(bool(result), "Motion fixture field allocation failed");
                return result;
            };
            auto motion =
                create(TEX_FORMAT_RGBA32_FLOAT, BIND_RENDER_TARGET | BIND_SHADER_RESOURCE);
            auto depth = create(TEX_FORMAT_D32_FLOAT, BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE);
            auto captured_depth =
                create(TEX_FORMAT_D32_FLOAT, BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE);
            forge::Camera settings;
            settings.projection = perspective ? 0u : 1u;
            settings.near_plane = .1;
            settings.far_plane = 200;
            auto camera = forge::camera_view(settings, {}, width, height);
            for (unsigned scenario = 0; scenario < 17; ++scenario) {
                const float vx = scenario == 14   ? std::numeric_limits<float>::quiet_NaN()
                                 : scenario == 15 ? std::numeric_limits<float>::infinity()
                                 : scenario == 4  ? -12
                                 : scenario == 5  ? 6
                                 : scenario == 6  ? 100
                                                  : 12;
                const float vy = scenario == 5 ? 8 : 0;
                const unsigned mode = scenario == 7 ? 1 : scenario == 8 ? 2 : scenario == 9 ? 3 : 0;
                forge::SceneMotionBlur blur{true, 1, 32};
                if (scenario == 0)
                    blur.enabled = false;
                if (scenario == 1)
                    blur.shutter = 0;
                if (scenario == 2)
                    blur.max_pixels = 0;
                if (scenario == 5)
                    blur.shutter = .5;
                if (scenario == 6)
                    blur.max_pixels = 10;
                if (scenario == 16)
                    blur.max_pixels = .25f;
                presentation.begin_frame();
                producer.draw(context, color, motion, depth, camera, scenario == 3 ? 0 : vx, vy,
                              mode, scenario == 10 ? 0 : 1);
                context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
                context->ClearDepthStencil(
                    captured_depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), CLEAR_DEPTH_FLAG, 1,
                    0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                forge::MotionBlur::Input input;
                input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                input.motion = motion->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                input.projection = {camera.projection[10], camera.projection[11],
                                    camera.projection[14], camera.projection[15]};
                input.settings = blur;
                if (scenario == 11)
                    input.motion_depth =
                        captured_depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                if (scenario == 12)
                    input.motion_offset = {.49f, -.49f};
                if (scenario == 13)
                    input.motion_offset = {-.49f, .49f};
                effect.apply(context, input, output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                presentation.end_frame(context);
                const auto original = forge::test_bloom::read(device, context, color);
                const auto actual = forge::test_bloom::read(device, context, output);
                const bool unchanged = scenario <= 3 || scenario == 9 || scenario == 10 ||
                                       scenario == 11 || scenario >= 14;
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width; ++x) {
                        const auto i = y * width + x;
                        check(actual[i][3] == original[i][3],
                              "Motion filter changed current alpha");
                        if (unchanged || y < 2 || mode == 1 || mode == 2)
                            check(actual[i] == original[i],
                                  "Motion crossed a depth/velocity boundary or changed stationary "
                                  "pixels");
                    }
                if (!unchanged && mode == 0) {
                    double dx = vx * blur.shutter, dy = vy * blur.shutter;
                    const auto length = std::hypot(dx, dy);
                    if (length > blur.max_pixels) {
                        dx *= blur.max_pixels / length;
                        dy *= blur.max_pixels / length;
                    }
                    for (unsigned y = 10; y < height - 10; ++y)
                        for (unsigned x = 20; x < width - 20; ++x) {
                            const auto& value = actual[y * width + x];
                            check(
                                std::abs(value[0] - (x + .5 * dx) / 64) < .0011 &&
                                    std::abs(value[1] - (y + .5 * dy) / 32) < .0011,
                                "Linear image exposure has wrong direction, shutter or trail cap");
                            if (scenario != 5)
                                check(std::abs(value[2] - .25) < .0011,
                                      "Uniform even checker exposure did not average");
                        }
                }
                bool rejected = false;
                try {
                    effect.apply(context, input, color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                } catch (const std::invalid_argument&) {
                    rejected = true;
                }
                check(rejected, "Motion admitted an in-place feedback loop");
                context->FinishFrame();
                device->ReleaseStaleResources();
                ++cases;
            }
        }
    std::cout << cases
              << " motion filter frames PASS: independent analytic exposure, depth/velocity "
                 "boundaries, alpha, disabled/zero, raster offset and resize/rebinding\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Motion probe Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        exercise(presentation, context);
        check(argc == 3, "Expected isolated motion Scene and Gallery directories");
        forge::test_motion_scene::exercise(presentation, context, argv[1]);
        forge::test_motion_scene::gallery(presentation, context, argv[2],
                                          std::filesystem::path(argv[1]) / "images");
        context->WaitForIdle();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
