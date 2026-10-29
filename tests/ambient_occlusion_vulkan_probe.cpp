#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "ambient_occlusion.hpp"
#include "ambient_occlusion_boundary_tests.hpp"
#include "ambient_occlusion_gallery_tests.hpp"
#include "ambient_occlusion_normal_tests.hpp"
#include "ambient_occlusion_scene_tests.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "indirect_mesh_output_tests.hpp"
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
        shader.Desc.Name = "FORGE analytic ambient fixture";
        shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
            return float4(id==2?3:-1,id==1?3:-1,0,1);})";
        RefCntAutoPtr<IShader> vertex, pixel;
        presentation.shader(shader, &vertex);
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        // Two parallel, overlapping half-planes have independently known view
        // distances. No algorithm from the occlusion operator generates them.
        shader.Source = R"(cbuffer Fixture {float4 Extent;float4 Depth;float4 Horizontal;};
        struct Result {float4 color:SV_Target0;float4 indirect:SV_Target1;float depth:SV_Depth;};
        Result main(float4 p:SV_Position) {
            int2 at=int2(p.xy);float z=10;
            float w=Depth.z*9.7+Depth.w;
            float boundary=(Horizontal.z*9.7+Horizontal.w)/w;
            bool front=int(Extent.z)==1 && (p.x/Extent.x*2-1)>=boundary;
            if(front)z=9.7;
            Result r;r.color=float4(1,.75,.5,(at.x+at.y)%2?.25:.75);
            r.indirect=Extent.w!=0?float4(.5,.25,.125,0):0;
            r.depth=int(Extent.z)==2?1:(Depth.x*z+Depth.y)/(Depth.z*z+Depth.w);
            return r;
        })";
        presentation.shader(shader, &pixel);
        BufferDesc desc;
        desc.Name = "FORGE ambient fixture parameters";
        desc.Size = 48;
        desc.BindFlags = BIND_UNIFORM_BUFFER;
        desc.Usage = USAGE_DYNAMIC;
        desc.CPUAccessFlags = CPU_ACCESS_WRITE;
        presentation.device()->CreateBuffer(desc, nullptr, &values_);
        GraphicsPipelineStateCreateInfo info;
        info.PSODesc.Name = "FORGE ambient fixture planes";
        info.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        auto& g = info.GraphicsPipeline;
        g.NumRenderTargets = 2;
        g.RTVFormats[0] = g.RTVFormats[1] = TEX_FORMAT_RGBA16_FLOAT;
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
        check(values_ && binding_, "Ambient fixture parameters/pipeline allocation failed");
        binding_->GetVariableByName(SHADER_TYPE_PIXEL, "Fixture")->Set(values_);
    }
    void draw(IDeviceContext* context, ITexture* color, ITexture* indirect, ITexture* depth,
              const forge::CameraView& camera, unsigned mode, bool environment) {
        const auto& d = color->GetDesc();
        const auto& p = camera.projection;
        {
            MapHelper<float> values(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
            values[0] = float(d.Width);
            values[1] = float(d.Height);
            values[2] = float(mode);
            values[3] = float(environment);
            values[4] = p[10];
            values[5] = p[11];
            values[6] = p[14];
            values[7] = p[15];
            values[8] = p[0];
            values[9] = p[1];
            values[10] = p[2];
            values[11] = p[3];
        }
        ITextureView* targets[]{color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                indirect->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
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
    forge::AmbientOcclusion effect(presentation);
    unsigned cases = 0, normal_cases = 0;
    for (unsigned projection : {0u, 1u, 2u})
        for (unsigned size : {0u, 1u})
            for (bool jitter : {false, true}) {
                const unsigned width = size ? 129 : 97, height = size ? 97 : 81;
                const std::vector<forge::test_bloom::Pixel> blank(width * height);
                auto color = forge::test_bloom::texture(device, width, height, blank);
                auto indirect = forge::test_bloom::texture(device, width, height, blank);
                auto output = forge::test_bloom::texture(device, width, height, blank);
                TextureDesc desc;
                desc.Name = "FORGE ambient fixture depth";
                desc.Type = RESOURCE_DIM_TEX_2D;
                desc.Width = width;
                desc.Height = height;
                desc.Format = TEX_FORMAT_D32_FLOAT;
                desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
                RefCntAutoPtr<ITexture> depth;
                device->CreateTexture(desc, nullptr, &depth);
                check(bool(depth), "Ambient fixture depth allocation failed");
                forge::Camera settings;
                settings.projection = projection ? 0u : 1u;
                settings.infinite_far = projection == 2;
                settings.orthographic_height = 4;
                settings.near_plane = .1;
                settings.far_plane = 200;
                auto camera = forge::camera_view(settings, {}, width, height);
                if (jitter)
                    for (unsigned c = 0; c < 4; ++c) {
                        camera.projection[c] += (.8f / width) * camera.projection[12 + c];
                        camera.projection[4 + c] += (-.6f / height) * camera.projection[12 + c];
                    }
                std::vector<forge::test_bloom::Pixel> full;
                for (unsigned scenario = 0; scenario < 8; ++scenario) {
                    const unsigned mode = scenario == 0 ? 0 : scenario == 1 ? 2 : 1;
                    const bool environment = scenario != 2;
                    forge::SceneAmbientOcclusion ao{true, 1, 1, .01f};
                    if (scenario == 3)
                        ao.enabled = false;
                    if (scenario == 4)
                        ao.radius = 0;
                    if (scenario == 5)
                        ao.strength = 0;
                    if (scenario == 7)
                        ao.strength = .5;
                    presentation.begin_frame();
                    producer.draw(context, color, indirect, depth, camera, mode, environment);
                    forge::AmbientOcclusion::Input input;
                    input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.indirect = indirect->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.projection = camera.projection;
                    input.settings = ao;
                    effect.apply(context, input,
                                 output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                    presentation.end_frame(context);
                    const auto original = forge::test_bloom::read(device, context, color);
                    const auto actual = forge::test_bloom::read(device, context, output);
                    std::size_t changed = 0;
                    const float boundary = (camera.projection[2] * 9.7f + camera.projection[3]) /
                                           (camera.projection[14] * 9.7f + camera.projection[15]);
                    for (unsigned y = 0; y < height; ++y)
                        for (unsigned x = 0; x < width; ++x) {
                            const auto i = y * width + x;
                            changed += actual[i] != original[i];
                            check(actual[i][3] == original[i][3],
                                  "Ambient operator changed current alpha");
                            if (scenario < 6 || ((x + .5) / width * 2 - 1) >= boundary)
                                check(actual[i] == original[i],
                                      "Ambient operator shaded a plane, sky, direct light or "
                                      "unoccluded front surface");
                            for (unsigned c = 0; c < 3; ++c) {
                                constexpr float radiance[]{.5f, .25f, .125f};
                                check(std::isfinite(actual[i][c]) &&
                                          actual[i][c] >= original[i][c] - radiance[c] - .0001f &&
                                          actual[i][c] <= original[i][c],
                                      "Ambient exposure changed more than the available "
                                      "environment radiance");
                                if (scenario == 7)
                                    check(std::abs((original[i][c] - actual[i][c]) -
                                                   .5f * (original[i][c] - full[i][c])) < .0011f,
                                          "Ambient strength did not scale the isolated indirect "
                                          "contribution");
                            }
                        }
                    if (scenario >= 6 && changed <= height)
                        throw std::runtime_error(
                            "Depth-step signal too small: projection=" +
                            std::to_string(projection) + ", size=" + std::to_string(width) + "x" +
                            std::to_string(height) + ", jitter=" + std::to_string(jitter) +
                            ", scenario=" + std::to_string(scenario) +
                            ", changed=" + std::to_string(changed));
                    if (scenario == 6)
                        full = actual;
                    bool rejected = false;
                    try {
                        effect.apply(context, input,
                                     color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                    } catch (const std::invalid_argument&) {
                        rejected = true;
                    }
                    check(rejected, "Ambient operator admitted an in-place feedback loop");
                    context->FinishFrame();
                    device->ReleaseStaleResources();
                    if (scenario == 0 || scenario == 2 || scenario == 6)
                        normal_cases += forge::test_occlusion::normal_inputs(
                            presentation, context, effect, input,
                            output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), original, actual,
                            scenario == 6);
                    ++cases;
                }
            }
    std::cout << normal_cases
              << " material-normal AO frames PASS: geometric/default/invalid, "
                 "flat/direct/alpha bounds, hemisphere direction and typed admission\n";
    std::cout << cases
              << " independent ambient plane/step frames PASS: geometry, direct/emissive floor, "
                 "sky/alpha, strength, jitter, perspective/orthographic/infinite and zero paths\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Ambient probe Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        if (argc == 4 && std::string_view(argv[3]) == "--normal-gallery") {
            forge::test_occlusion_scene::gallery(presentation, context, argv[2], argv[1], true);
            context->WaitForIdle();
            return 0;
        }
        forge::test_occlusion::boundaries(presentation, context);
        exercise(presentation, context);
        forge::test_indirect_mesh::exercise(presentation, context);
        check(argc == 3, "Expected isolated ambient Scene and Gallery directories");
        forge::test_occlusion_scene::exercise(presentation, context,
                                              std::filesystem::path(argv[1]));
        forge::test_occlusion_scene::gallery(presentation, context, std::filesystem::path(argv[2]),
                                             std::filesystem::path(argv[1]) / "images");
        context->WaitForIdle();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
