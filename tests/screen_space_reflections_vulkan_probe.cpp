#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "reflection_boundary_tests.hpp"
#include "reflection_material_output_tests.hpp"
#include "surface_reflections.hpp"
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
    float wallX=(ndc*(P3.z*3+P3.w)-P0.z*3-P0.w)/P0.x;
    bool receiver=wallX<=0||Extent.z==2;
    float z=receiver?(2+2*(ndc*P3.w-P0.w)/P0.x)/(1-2*(ndc*P3.z-P0.z)/P0.x):3;
    float d=(P2.z*z+P2.w)/(P3.z*z+P3.w);
    r.depth=Extent.z==1||z<=.1||!(d>=0&&d<1)?1:d;
    float alpha=(int(p.x)+int(p.y))%2?.25:.75;
    r.color=float4(receiver?float3(.7,.5,.7):float3(.8,.4,.2),alpha);
    r.indirect=float4(receiver?float3(.2,.2,.2):float3(.05,.05,.05),0);
    r.surface=receiver?float4(normalize(float3(2,0,-1)),Extent.z==4?1:.1):0;
    if(Extent.z==5)r.surface.xyz=float3(0,0,1);
    r.response=receiver&&Extent.z!=3?float4(.5,.25,.75,0):0;
    r.specular=receiver?float4(.05,.05,.05,0):0;
    return r;
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
              ITexture* depth, const forge::CameraView& camera, unsigned mode) {
        const auto& size = targets[0]->GetDesc();
        {
            MapHelper<float> data(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
            data[0] = float(size.Width);
            data[1] = float(size.Height);
            data[2] = float(mode);
            data[3] = 0;
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
void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    Producer producer(presentation);
    forge::SurfaceReflections effect(presentation);
    unsigned cases = 0;
    for (unsigned projection = 0; projection < 3; ++projection)
        for (bool larger : {false, true})
            for (bool jitter : {false, true}) {
                const unsigned width = larger ? 129 : 97, height = larger ? 97 : 81;
                const std::vector<forge::test_bloom::Pixel> blank(width * height);
                std::array<RefCntAutoPtr<ITexture>, 5> targets;
                for (auto& target : targets)
                    target = forge::test_bloom::texture(device, width, height, blank);
                auto output = forge::test_bloom::texture(device, width, height, blank);
                auto indirect = forge::test_bloom::texture(device, width, height, blank);
                TextureDesc d;
                d.Name = "FORGE reflection fixture depth";
                d.Type = RESOURCE_DIM_TEX_2D;
                d.Width = width;
                d.Height = height;
                d.Format = TEX_FORMAT_D32_FLOAT;
                d.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
                RefCntAutoPtr<ITexture> depth;
                device->CreateTexture(d, nullptr, &depth);
                check(bool(depth), "Reflection fixture depth allocation failed");
                forge::Camera settings;
                settings.projection = projection == 0 ? 1u : 0u;
                settings.orthographic_height = 4;
                settings.near_plane = .1;
                settings.far_plane = 100;
                settings.infinite_far = projection == 2;
                auto camera = forge::camera_view(settings, {}, width, height);
                if (jitter)
                    for (unsigned c = 0; c < 4; ++c) {
                        camera.projection[c] += .3f / width * camera.projection[12 + c];
                        camera.projection[4 + c] -= .4f / height * camera.projection[12 + c];
                    }
                std::vector<forge::test_bloom::Pixel> full, full_indirect;
                for (unsigned scenario = 0; scenario < 12; ++scenario) {
                    const unsigned mode = scenario == 4    ? 1
                                          : scenario == 5  ? 3
                                          : scenario == 9  ? 4
                                          : scenario == 10 ? 5
                                          : scenario == 11 ? 2
                                                           : 0;
                    forge::SurfaceReflections::Input input;
                    input.color = targets[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.indirect = targets[1]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.surface = targets[2]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.response = targets[3]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.specular = targets[4]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    input.projection = camera.projection;
                    input.settings = {true, 8, .1f, 1, .5f};
                    if (scenario == 0)
                        input.settings.enabled = false;
                    if (scenario == 1)
                        input.settings.strength = 0;
                    if (scenario == 2)
                        input.settings.max_distance = 0;
                    if (scenario == 3)
                        input.settings.max_roughness = 0;
                    if (scenario == 6)
                        input.settings.max_distance = .1f;
                    if (scenario == 8)
                        input.settings.strength = .5f;
                    presentation.begin_frame();
                    producer.draw(context, targets, depth, camera, mode);
                    effect.apply(context, input, output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                 indirect->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
                    presentation.end_frame(context);
                    const auto original = forge::test_bloom::read(device, context, targets[0]);
                    const auto old_indirect = forge::test_bloom::read(device, context, targets[1]);
                    const auto actual = forge::test_bloom::read(device, context, output);
                    const auto current_indirect =
                        forge::test_bloom::read(device, context, indirect);
                    std::size_t changed = 0;
                    for (std::size_t at = 0; at < actual.size(); ++at) {
                        changed += actual[at] != original[at];
                        check(actual[at][3] == original[at][3] &&
                                  current_indirect[at][3] == old_indirect[at][3],
                              "Reflection composition changed current alpha");
                        if (scenario != 7 && scenario != 8)
                            check(actual[at] == original[at] &&
                                      current_indirect[at] == old_indirect[at],
                                  "Reflection miss/zero/rough/normal/plane path changed current "
                                  "lighting");
                        for (unsigned c = 0; c < 3; ++c) {
                            const double change = actual[at][c] - original[at][c];
                            const double indirect_change =
                                current_indirect[at][c] - old_indirect[at][c];
                            check(std::isfinite(actual[at][c]) &&
                                      std::abs(change - indirect_change) < .0015,
                                  "Reflection changed direct/emissive light or failed to update AO "
                                  "input");
                            if (scenario == 8) {
                                check(std::abs(change - .5 * (full[at][c] - original[at][c])) <
                                          .0015,
                                      "Reflection strength does not scale its full-color "
                                      "replacement");
                                check(
                                    std::abs(indirect_change - .5 * (full_indirect[at][c] -
                                                                     old_indirect[at][c])) < .0015,
                                    "Reflection strength does not scale updated indirect radiance");
                            }
                        }
                    }
                    if (scenario == 7 || scenario == 8) {
                        if (changed <= height)
                            throw std::runtime_error(
                                "Reflection wall signal too small: projection=" +
                                std::to_string(projection) + ", width=" + std::to_string(width) +
                                ", jitter=" + std::to_string(jitter) + ", scenario=" +
                                std::to_string(scenario) + ", changed=" + std::to_string(changed));
                    }
                    if (scenario == 7) {
                        full = actual;
                        full_indirect = current_indirect;
                    }
                    context->FinishFrame();
                    device->ReleaseStaleResources();
                    ++cases;
                }
            }
    context->WaitForIdle();
    std::cout
        << cases
        << " independent reflection receiver/wall frames PASS: hit/miss, zero paths, geometry, "
           "projection/jitter, roughness, direct/emissive/alpha and updated indirect radiance\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Reflection Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        if (argc == 2 && std::string_view(argv[1]) == "--material")
            forge::test_reflection_material::exercise(presentation, context);
        else {
            check(argc == 1, "Expected no arguments or --material");
            forge::test_reflection::boundaries(presentation, context);
            exercise(presentation, context);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
