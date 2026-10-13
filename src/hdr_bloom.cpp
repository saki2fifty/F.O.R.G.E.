#include "hdr_bloom.hpp"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "hdr_bloom_shaders.hpp"
#include <algorithm>
namespace forge {
using namespace Diligent;
HdrBloom::HdrBloom(IRenderDevice* device, ShaderFactory shader, PipelineFactory pipeline,
                   std::uint64_t payload_budget)
    : device_(device), shader_(std::move(shader)), pipeline_(std::move(pipeline)),
      budget_(payload_budget) {
    if (!device_ || !shader_ || !pipeline_ || !budget_ || budget_ > hdr_bloom_payload_limit)
        throw std::runtime_error("Bloom requires its presentation device and creation policy");
}
void HdrBloom::prepare() {
    if (values_)
        return;
    std::array<RefCntAutoPtr<IPipelineState>, 3> passes;
    std::array<RefCntAutoPtr<IShaderResourceBinding>, 3> bindings;
    ShaderCreateInfo shader;
    shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
    shader.EntryPoint = "main";
    shader.Desc.Name = "FORGE bloom vertex";
    shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
    shader.Source = bloom_shader::vertex;
    RefCntAutoPtr<IShader> vertex;
    shader_(shader, &vertex);
    if (!vertex)
        throw std::runtime_error("Bloom vertex shader creation failed");
    const char* names[]{"FORGE bloom extract", "FORGE bloom blur", "FORGE bloom composite"};
    const char* functions[]{bloom_shader::extract, bloom_shader::blur, bloom_shader::composite};
    for (unsigned i = 0; i < 3; ++i) {
        const std::string source = std::string(bloom_shader::common) + functions[i];
        shader.Source = source.c_str();
        shader.Desc.Name = names[i];
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        RefCntAutoPtr<IShader> pixel;
        shader_(shader, &pixel);
        if (!pixel)
            throw std::runtime_error("Bloom pixel shader creation failed");
        GraphicsPipelineStateCreateInfo ci;
        ci.PSODesc.Name = names[i];
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        ShaderResourceVariableDesc variables[]{
            {SHADER_TYPE_PIXEL, "g_Source", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "g_Original", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
        ci.PSODesc.ResourceLayout.Variables = variables;
        ci.PSODesc.ResourceLayout.NumVariables = i == 2 ? 2 : 1;
        ci.GraphicsPipeline.NumRenderTargets = 1;
        ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA16_FLOAT;
        ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
        ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
        ci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
        ci.pVS = vertex;
        ci.pPS = pixel;
        pipeline_(ci, &passes[i]);
        if (!passes[i])
            throw std::runtime_error("Bloom pipeline creation failed");
        passes[i]->CreateShaderResourceBinding(&bindings[i], true);
        if (!bindings[i] || !bindings[i]->GetVariableByName(SHADER_TYPE_PIXEL, "g_Source") ||
            !bindings[i]->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeBloom") ||
            (i == 2 && !bindings[i]->GetVariableByName(SHADER_TYPE_PIXEL, "g_Original")))
            throw std::runtime_error("Bloom shader interface is incomplete");
    }
    BufferDesc desc;
    desc.Name = "FORGE spatial bloom parameters";
    desc.Size = 32;
    desc.BindFlags = BIND_UNIFORM_BUFFER;
    desc.Usage = USAGE_DYNAMIC;
    desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    RefCntAutoPtr<IBuffer> values;
    device_->CreateBuffer(desc, nullptr, &values);
    if (!values)
        throw std::runtime_error("Bloom parameter buffer creation failed");
    for (auto& binding : bindings)
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "ForgeBloom")->Set(values);
    passes_ = std::move(passes);
    bindings_ = std::move(bindings);
    values_ = std::move(values);
}
HdrBloom::Targets& HdrBloom::targets(const HdrBloomPlan& plan) {
    for (auto& cached : cached_targets_)
        if (cached.snapshot && cached.snapshot->GetDesc().Width == plan.width &&
            cached.snapshot->GetDesc().Height == plan.height) {
            cached.used = ++uses_;
            return cached;
        }
    const auto& support = device_->GetTextureFormatInfoExt(TEX_FORMAT_RGBA16_FLOAT);
    const auto required = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    if ((support.BindFlags & required) != required)
        throw std::runtime_error("Device cannot render and sample half-float HDR bloom");
    TextureDesc desc;
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = required;
    desc.Width = plan.width;
    desc.Height = plan.height;
    desc.Name = "FORGE bloom view snapshot";
    RefCntAutoPtr<ITexture> snapshot, first, second;
    device_->CreateTexture(desc, nullptr, &snapshot);
    desc.Width = plan.half_width;
    desc.Height = plan.half_height;
    desc.Name = "FORGE bloom spatial target A";
    device_->CreateTexture(desc, nullptr, &first);
    desc.Name = "FORGE bloom spatial target B";
    device_->CreateTexture(desc, nullptr, &second);
    if (!snapshot || !first || !second)
        throw std::runtime_error("Bloom target allocation failed; previous targets retained");
    Targets* slot = nullptr;
    while (!slot) {
        for (auto& cached : cached_targets_)
            if (!cached.snapshot) {
                slot = &cached;
                break;
            }
        if (!slot || plan.bytes > budget_ - payload_) {
            auto oldest = std::min_element(cached_targets_.begin(), cached_targets_.end(),
                                           [](const auto& a, const auto& b) {
                                               if (bool(a.snapshot) != bool(b.snapshot))
                                                   return bool(a.snapshot);
                                               return a.used < b.used;
                                           });
            if (!oldest->snapshot)
                throw std::runtime_error("Bloom cache payload invariant failed");
            payload_ -= oldest->bytes;
            *oldest = {};
            slot = nullptr;
        }
    }
    *slot = {std::move(snapshot), std::move(first), std::move(second), plan.bytes, ++uses_};
    payload_ += plan.bytes;
    allocations_ += 3;
    return *slot;
}
void HdrBloom::draw(IDeviceContext* context, unsigned pass, ITextureView* source,
                    ITextureView* target, PixelViewport area, const SceneBloom& settings,
                    bool horizontal) {
    {
        MapHelper<float> data(context, values_, MAP_WRITE, MAP_FLAG_DISCARD);
        data[0] = settings.threshold;
        data[1] = settings.intensity;
        data[2] = settings.radius;
        data[3] = horizontal ? 1 : 0;
        data[4] = float(area.x);
        data[5] = float(area.y);
        data[6] = float(area.width);
        data[7] = float(area.height);
    }
    auto& binding = bindings_[pass];
    binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Source")->Set(source);
    if (pass == 2)
        binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Original")
            ->Set(active_->snapshot->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const auto& texture = target->GetTexture()->GetDesc();
    Diligent::Viewport viewport{
        float(area.x), float(area.y), float(area.width), float(area.height), 0, 1};
    context->SetViewports(1, &viewport, texture.Width, texture.Height);
    context->SetPipelineState(passes_[pass]);
    context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
    ++draws_;
}
void HdrBloom::apply(IDeviceContext* context, ITexture* source, PixelViewport area,
                     const SceneBloom& settings) {
    if (thread_ != std::this_thread::get_id() || !context || !source)
        throw std::runtime_error("Bloom requires its presentation thread, context and HDR source");
    validate_scene_bloom(settings);
    if (!settings.enabled || settings.intensity == 0)
        return;
    const auto& input = source->GetDesc();
    if (input.Type != RESOURCE_DIM_TEX_2D || input.Format != TEX_FORMAT_RGBA16_FLOAT ||
        input.SampleCount != 1 || input.MipLevels != 1 || !(input.BindFlags & BIND_RENDER_TARGET))
        throw std::runtime_error("Bloom requires a distinct single-mip linear HDR render target");
    for (const auto& cached : cached_targets_)
        if (source == cached.snapshot.RawPtr() || source == cached.first.RawPtr() ||
            source == cached.second.RawPtr())
            throw std::runtime_error("Bloom source aliases a private pass target");
    const auto plan =
        hdr_bloom_plan(input.Width, input.Height, area,
                       device_->GetAdapterInfo().Texture.MaxTexture2DDimension, budget_);
    prepare();
    active_ = &targets(plan);
    Box box{area.x, area.x + area.width, area.y, area.y + area.height};
    CopyTextureAttribs copy{source, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, active_->snapshot,
                            RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
    copy.pSrcBox = &box;
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture(copy);
    const PixelViewport half{0, 0, plan.half_width, plan.half_height};
    auto view = [](auto& texture, TEXTURE_VIEW_TYPE type) { return texture->GetDefaultView(type); };
    draw(context, 0, view(active_->snapshot, TEXTURE_VIEW_SHADER_RESOURCE),
         view(active_->first, TEXTURE_VIEW_RENDER_TARGET), half, settings, false);
    draw(context, 1, view(active_->first, TEXTURE_VIEW_SHADER_RESOURCE),
         view(active_->second, TEXTURE_VIEW_RENDER_TARGET), half, settings, true);
    draw(context, 1, view(active_->second, TEXTURE_VIEW_SHADER_RESOURCE),
         view(active_->first, TEXTURE_VIEW_RENDER_TARGET), half, settings, false);
    draw(context, 2, view(active_->first, TEXTURE_VIEW_SHADER_RESOURCE),
         source->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), area, settings, false);
}
} // namespace forge
