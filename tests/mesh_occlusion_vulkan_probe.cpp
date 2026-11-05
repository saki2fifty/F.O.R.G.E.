#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "mesh_occlusion.hpp"
#include "sprite_draw_render_tests.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
using namespace Diligent;
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class T>
std::vector<T> read(IRenderDevice* device, IDeviceContext* context, IBuffer* source,
                    std::size_t count) {
    BufferDesc desc;
    desc.Name = "FORGE test-only occlusion readback";
    desc.Size = count * sizeof(T);
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> staging;
    device->CreateBuffer(desc, nullptr, &staging);
    check(bool(staging), "Occlusion staging allocation failed");
    context->CopyBuffer(source, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, desc.Size,
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    void* mapped = nullptr;
    context->MapBuffer(staging, MAP_READ, MAP_FLAG_NONE, mapped);
    check(mapped != nullptr, "Occlusion staging map failed");
    std::vector<T> result(count);
    std::memcpy(result.data(), mapped, desc.Size);
    context->UnmapBuffer(staging, MAP_READ);
    return result;
}
RefCntAutoPtr<ITexture> depth(IRenderDevice* device, unsigned width, unsigned height,
                              const std::vector<float>& data) {
    TextureDesc desc;
    desc.Name = "FORGE current-depth occlusion oracle";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_R32_FLOAT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    TextureSubResData row;
    row.pData = data.data();
    row.Stride = width * sizeof(float);
    Diligent::TextureData initial{&row, 1};
    RefCntAutoPtr<ITexture> texture;
    device->CreateTexture(desc, &initial, &texture);
    check(bool(texture), "Occlusion input allocation failed");
    return texture;
}
void hierarchy_oracle(IRenderDevice* device, IDeviceContext* context, MeshOcclusion& culler,
                      const std::vector<float>& input, unsigned source_width,
                      const PixelViewport& viewport) {
    const auto* plan = culler.plan();
    check(plan != nullptr, "Occlusion active plan unavailable");
    const auto values = read<float>(device, context, culler.hierarchy(), plan->pyramid_bytes / 4);
    unsigned width = plan->pyramid_width, height = plan->pyramid_height, offset = 0;
    for (unsigned level = 0; level < plan->levels; ++level) {
        // Independent oracle: integrate each cell's full source footprint directly,
        // never run the producer's recursive four-parent reduction on the CPU.
        const unsigned scale = 2u << level;
        const unsigned span_x = std::min(scale, plan->pyramid_width * 2);
        const unsigned span_y = std::min(scale, plan->pyramid_height * 2);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                float expected = 0;
                for (unsigned dy = 0; dy < span_y; ++dy)
                    for (unsigned dx = 0; dx < span_x; ++dx) {
                        const unsigned px = x * scale + dx, py = y * scale + dy;
                        float value = 1;
                        if (px < viewport.width && py < viewport.height)
                            value = input[(py + viewport.y) * source_width + px + viewport.x];
                        if (!(value >= 0 && value <= 1))
                            value = 1;
                        expected = std::max(expected, value);
                    }
                check(values.at(offset + y * width + x) == expected,
                      "Hierarchy differs from direct full-footprint far-depth coverage");
            }
        offset += width * height;
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
    }
}
void indirect_pixels(DiligentPresentation& presentation, IDeviceContext* context,
                     MeshOcclusion& culler, std::span<const bool> hidden) {
    auto* device = presentation.device();
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE indirect occlusion oracle vertex";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = R"(float4 main(uint id:SV_VertexID):SV_Position {
        float2 p[3]={float2(-.8,-.8),float2(0,.8),float2(.8,-.8)};
        return float4(p[id%3],0,1);})";
    RefCntAutoPtr<IShader> vertex, pixel;
    presentation.shader(shader, &vertex);
    shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
    shader.Desc.Name = "FORGE indirect occlusion oracle color";
    shader.Source = "float4 main():SV_Target0{return float4(0,1,0,1);}";
    presentation.shader(shader, &pixel);
    GraphicsPipelineStateCreateInfo info;
    info.PSODesc.Name = "FORGE actual indirect occlusion pixels";
    info.pVS = vertex;
    info.pPS = pixel;
    info.GraphicsPipeline.NumRenderTargets = 1;
    info.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    info.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    info.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    info.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    info.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    RefCntAutoPtr<IPipelineState> pipeline;
    RefCntAutoPtr<IShaderResourceBinding> binding;
    presentation.graphics(info, &pipeline);
    pipeline->CreateShaderResourceBinding(&binding, true);
    const std::uint16_t indices[]{0, 1, 2};
    BufferDesc index_desc;
    index_desc.Name = "FORGE indirect index oracle";
    index_desc.Size = sizeof(indices);
    index_desc.Usage = USAGE_IMMUTABLE;
    index_desc.BindFlags = BIND_INDEX_BUFFER;
    BufferData initial{indices, sizeof(indices)};
    RefCntAutoPtr<IBuffer> index_buffer;
    device->CreateBuffer(index_desc, &initial, &index_buffer);
    TextureDesc target_desc;
    target_desc.Name = "FORGE indirect result pixels";
    target_desc.Type = RESOURCE_DIM_TEX_2D;
    target_desc.Width = target_desc.Height = 16;
    target_desc.Format = TEX_FORMAT_RGBA8_UNORM;
    target_desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> target;
    device->CreateTexture(target_desc, nullptr, &target);
    check(bool(index_buffer) && bool(target), "Indirect pixel resources unavailable");
    auto* view = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    for (bool indexed : {false, true})
        for (unsigned i = 0; i < hidden.size(); ++i) {
            context->SetRenderTargets(1, &view, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[]{0, 0, 0, 0};
            context->ClearRenderTarget(view, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->SetPipelineState(pipeline);
            context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Viewport area{0, 0, 16, 16, 0, 1};
            context->SetViewports(1, &area, 16, 16);
            if (indexed) {
                context->SetIndexBuffer(index_buffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                DrawIndexedIndirectAttribs draw;
                draw.IndexType = VT_UINT16;
                draw.pAttribsBuffer = culler.arguments();
                draw.DrawArgsOffset = i * sizeof(MeshOcclusionCommand);
                draw.DrawArgsStride = sizeof(MeshOcclusionCommand);
                draw.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                draw.Flags = DRAW_FLAG_VERIFY_ALL;
                context->DrawIndexedIndirect(draw);
            } else {
                DrawIndirectAttribs draw;
                draw.pAttribsBuffer = culler.arguments();
                draw.DrawArgsOffset = i * sizeof(MeshOcclusionCommand);
                draw.DrawArgsStride = sizeof(MeshOcclusionCommand);
                draw.AttribsBufferStateTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                draw.Flags = DRAW_FLAG_VERIFY_ALL;
                context->DrawIndirect(draw);
            }
            const auto pixels = test_sprite_draw::read_pixels(device, context, target);
            check((pixels.at(8 * 16 + 8)[1] > 200) == !hidden[i],
                  "Actual indexed/nonindexed indirect pixels differ from query visibility");
        }
}
void exercise(DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    MeshOcclusion culler(presentation);
    check(culler.supported(), "Occlusion oracle requires compute/basic indirect support");
    const PixelViewport viewport{0, 0, 13, 9};
    std::vector<float> data(13 * 9, .25f);
    const auto image = depth(device, 13, 9, data);
    const std::array<MeshOcclusionQuery, 12> queries{
        {{{0, 0, 4, 4}, .75f, 0, 1},
         {{0, 0, 4, 4}, .1f, 0, 1},
         {{0, 0, 4, 4}, .25f, 0, 1},
         {{0, 0, 4, 4}, .3f, .1f, 1},
         {{12, 8, 13, 9}, .9f, 0, 1},
         {{0, 0, 13, 9}, .9f, 0, 1},
         {{0, 0, 4, 4}, .75f, 0, 0},
         {{0, 0, 4, 4}, .75f, -.1f, 1},
         {{0, 0, 4, 4}, std::numeric_limits<float>::quiet_NaN(), 0, 1},
         {{0, 0, 14, 4}, .75f, 0, 1},
         {{0, 0, 0, 0}, .75f, 0, 1},
         {{0, 0, 4, 4}, .75f, 0, 2}}};
    std::vector<MeshOcclusionCommand> commands(queries.size());
    for (unsigned i = 0; i < commands.size(); ++i)
        commands[i].words = {7 + i, 2 + i, 17 + i, 31 + i, 47 + i};
    check(culler.build(context, image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), viewport,
                       queries, commands),
          "Occlusion build declined its valid fixture");
    hierarchy_oracle(device, context, culler, data, 13, viewport);
    const auto output =
        read<MeshOcclusionCommand>(device, context, culler.arguments(), commands.size());
    for (unsigned i = 0; i < commands.size(); ++i) {
        auto expected = commands[i];
        if (i == 0)
            expected.words[1] = 0;
        check(output[i].words == expected.words,
              "Occlusion overwrote non-count draw attributes or rejected a fail-open case");
        commands[i].words = {3, 1, 0, 0, 0};
    }
    auto* retained = culler.arguments();
    check(culler.build(context, image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), viewport,
                       queries, commands) &&
              culler.arguments() == retained,
          "Same-sized occlusion scratch was not reused");
    const std::array<bool, 12> hidden{true,  false, false, false, false, false,
                                      false, false, false, false, false, false};
    indirect_pixels(presentation, context, culler, hidden);
    std::fill(data.begin(), data.end(), .9f);
    const auto changed = depth(device, 13, 9, data);
    check(culler.build(context, changed->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), viewport,
                       queries, commands),
          "Changed depth did not rebuild");
    check(read<MeshOcclusionCommand>(device, context, culler.arguments(), commands.size())[0]
                  .words[1] == 1,
          "Current depth reused a stale hidden count");
    data.assign(13 * 9, .1f);
    const PixelViewport subview{8, 4, 4, 4};
    for (unsigned y = 4; y < 8; ++y)
        for (unsigned x = 8; x < 12; ++x)
            data[y * 13 + x] = .8f;
    const auto sub = depth(device, 13, 9, data);
    const std::array<MeshOcclusionQuery, 2> local{
        {{{0, 0, 4, 4}, .5f, 0, 1}, {{0, 0, 4, 4}, .9f, 0, 1}}};
    check(culler.build(context, sub->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), subview, local,
                       {commands.data(), 2}),
          "Subview occlusion declined");
    hierarchy_oracle(device, context, culler, data, 13, subview);
    const auto local_output = read<MeshOcclusionCommand>(device, context, culler.arguments(), 2);
    check(local_output[0].words[1] == 1 && local_output[1].words[1] == 0,
          "Subview origin leaked outside depth into visibility");
    data.assign(13 * 9, .25f);
    data[0] = std::numeric_limits<float>::quiet_NaN();
    const auto invalid_depth = depth(device, 13, 9, data);
    check(culler.build(context, invalid_depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                       viewport, queries, commands),
          "Invalid-depth fixture declined");
    hierarchy_oracle(device, context, culler, data, 13, viewport);
    check(read<MeshOcclusionCommand>(device, context, culler.arguments(), commands.size())[0]
                  .words[1] == 1,
          "Invalid depth became an occluder");
    TextureDesc native_depth;
    native_depth.Name = "FORGE actual depth-view occlusion";
    native_depth.Type = RESOURCE_DIM_TEX_2D;
    native_depth.Width = 13;
    native_depth.Height = 9;
    native_depth.Format = TEX_FORMAT_D32_FLOAT;
    native_depth.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> dsv;
    device->CreateTexture(native_depth, nullptr, &dsv);
    check(bool(dsv), "Native depth allocation failed");
    context->ClearDepthStencil(dsv->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), CLEAR_DEPTH_FLAG,
                               .25f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    check(culler.build(context, dsv->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), viewport,
                       queries, commands),
          "Native depth view was not admitted");
    check(read<MeshOcclusionCommand>(device, context, culler.arguments(), commands.size())[0]
                  .words[1] == 0,
          "Native depth view did not produce the hidden query");
    check(!culler.build(context, dsv->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), viewport, queries,
                        commands) &&
              !culler.arguments(),
          "Depth-stencil view was accepted as sampled depth or retained a stale mask");
    TextureDesc typeless = native_depth;
    typeless.Format = TEX_FORMAT_R32_TYPELESS;
    typeless.BindFlags = BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> integer_depth;
    device->CreateTexture(typeless, nullptr, &integer_depth);
    check(bool(integer_depth), "Typeless depth oracle allocation failed");
    TextureViewDesc integer_view;
    integer_view.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
    integer_view.Format = TEX_FORMAT_R32_UINT;
    RefCntAutoPtr<ITextureView> integer_sample;
    integer_depth->CreateView(integer_view, &integer_sample);
    check(bool(integer_sample) &&
              !culler.build(context, integer_sample, viewport, queries, commands) &&
              !culler.arguments(),
          "Integer depth view was treated as float or retained a stale mask");
    auto* prepared = culler.prepare_depth(13, 9);
    check(prepared && culler.prepare_depth(13, 9) == prepared && !culler.prepare_depth(0, 9) &&
              !culler.arguments() && !culler.prepare_depth(16384, 16384) &&
              culler.prepare_depth(13, 9) == prepared,
          "Depth scratch did not reuse/recover after invalid or oversized requests");
    // Exercise the largest argument upload and compare hidden results against
    // direct original-depth coverage, independently of the hierarchy classifier.
    std::vector<float> dense(16 * 16, .2f);
    dense[8 * 16 + 8] = 1;
    const auto dense_image = depth(device, 16, 16, dense);
    std::vector<MeshOcclusionQuery> many(mesh_occlusion_query_limit);
    std::vector<MeshOcclusionCommand> many_commands(many.size());
    std::mt19937 random(81273);
    for (unsigned i = 0; i < many.size(); ++i) {
        const unsigned x = random() % 13, y = random() % 13, w = 1 + random() % 4,
                       h = 1 + random() % 4;
        many[i] = {{x, y, x + w, y + h}, i % 3 ? .7f : .1f, 0, 1};
        many_commands[i].words = {3, 1 + i % 7, 0, 0, 0};
    }
    check(culler.build(context, dense_image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                       {0, 0, 16, 16}, many, many_commands),
          "Maximum query/argument upload declined");
    const auto results =
        read<MeshOcclusionCommand>(device, context, culler.arguments(), many.size());
    unsigned hidden_count = 0;
    for (unsigned i = 0; i < many.size(); ++i) {
        if (results[i].words[1])
            check(results[i].words == many_commands[i].words,
                  "Visible query changed its original draw command");
        else {
            ++hidden_count;
            const auto& q = many[i];
            for (unsigned y = q.rectangle[1]; y < q.rectangle[3]; ++y)
                for (unsigned x = q.rectangle[0]; x < q.rectangle[2]; ++x)
                    check(q.nearest_depth > dense[y * 16 + x] + q.depth_bias,
                          "Hierarchy falsely rejected a directly visible original-depth pixel");
        }
    }
    check(hidden_count > 100, "Maximum-query oracle performed no useful rejection");
    check(!culler.build(context, image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), {0, 0, 14, 9},
                        queries, commands) &&
              !culler.arguments(),
          "Declined view exposed a stale indirect mask");
    context->WaitForIdle();
    std::cout
        << "Far-depth hierarchy, invalid/odd/subview/fail-open cases, original attributes, current "
           "depth, 4096queries and actual indexed/nonindexed indirect pixels passed; rejected="
        << hidden_count << '\n';
}
} // namespace
int main() {
    try {
        EngineVkCreateInfo create;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Occlusion Vulkan device unavailable");
        DiligentPresentation presentation(device);
        exercise(presentation, context);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
