#include "ui_filter_layers.hpp"
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "ui_filter_admission.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace Diligent;
namespace forge {
using namespace ui_filter_detail;
struct UiFilterLayers::Impl {
    struct Image {
        RefCntAutoPtr<ITexture> texture;
        std::uint64_t bytes = 0, used = 0;
        bool active = false;
        unsigned pins = 0;
    };
    struct Constants {
        float viewport[4]{};
        float filter[4]{};
        float offset[4]{};
        float color[4]{};
        float weights[52]{};
    };
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<ITextureView> base;
    RefCntAutoPtr<IBuffer> constants;
    std::array<std::array<RefCntAutoPtr<IPipelineState>, 4>, 2> pipelines;
    RefCntAutoPtr<IShaderResourceBinding> base_binding;
    struct CompiledFilter {
        Filter effect;
        std::optional<std::size_t> mask;
        std::uint64_t frame = 0;
    };
    std::map<Rml::CompiledFilterHandle, CompiledFilter> filters;
    std::map<Rml::LayerHandle, std::size_t> layers;
    std::vector<Rml::LayerHandle> stack;
    std::vector<Image> images;
    std::uint64_t bytes = 0, allocations = 0, use = 0, frame = 0;
    std::uintptr_t next = 1;
    unsigned width = 0, height = 0, format = 0;
    explicit Impl(IRenderDevice* d) : device(d) {}
    void pipelines_ready() {
        if (constants)
            return;
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.Desc.UseCombinedTextureSamplers = True;
        shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        shader.Desc.Name = "FORGE UI layer VS";
        shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shader.Source = R"(float4 main(uint id:SV_VertexID):SV_POSITION {
float2 uv=float2((id<<1)&2,id&2);return float4(uv.x*2-1,1-uv.y*2,0,1);})";
        RefCntAutoPtr<IShader> vs, ps;
        device->CreateShader(shader, &vs);
        shader.Desc.Name = "FORGE UI filter PS";
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        // Layer pixels already have the destination color domain. They must not
        // pass through the geometry adapter's display-to-linear transfer again.
        shader.Source = R"(#include "SRGBUtilities.fxh"
cbuffer UiFilterData {float4 viewport;float4 filter;float4 offset;float4 shadow_color;float4 weights[13];};
Texture2D input_image;SamplerState input_image_sampler;
Texture2D mask_image;SamplerState mask_image_sampler;
float4 sample_image(float2 uv) {
    return any(uv<0)||any(uv>1) ? float4(0,0,0,0) : input_image.SampleLevel(input_image_sampler,uv,0);
}
float4 main(float4 position:SV_POSITION):SV_TARGET {
    float2 uv=position.xy/viewport.xy;
    int kind=int(filter.x);float value=filter.y;
    float4 color=sample_image(uv);
    if(kind==1) {
        color=0;int radius=int(filter.z);
        float2 step=filter.w==0 ? float2(1/viewport.x,0) : float2(0,1/viewport.y);
        for(int i=-radius;i<=radius;++i) {
            int at=abs(i);color+=sample_image(uv+step*i)*weights[at/4][at%4];
        }
    } else if(kind==2) color*=value;
    else if(kind==3) color.rgb*=value;
    else if(kind==4) color.rgb=max(float3(0,0,0),(color.rgb-.5*color.a)*value+.5*color.a);
    else if(kind==5) color.rgb=lerp(color.rgb,dot(color.rgb,float3(.2126,.7152,.0722)).xxx,value);
    else if(kind==6) {
        float4 tint=shadow_color;
        if(viewport.z>0.5) tint.rgb=SRGBToLinear(tint.rgb);
        tint.rgb*=tint.a;
        color=tint*sample_image(uv-offset.xy/viewport.xy).a;
    }
    else if(kind==7) color*=mask_image.SampleLevel(mask_image_sampler,uv,0).a;
    return color;
})";
        device->CreateShader(shader, &ps);
        if (!vs || !ps)
            throw std::runtime_error("UI filter shader compilation failed");
        BufferDesc buffer;
        buffer.Name = "FORGE UI filter constants";
        buffer.Size = sizeof(Constants);
        buffer.Usage = USAGE_DYNAMIC;
        buffer.BindFlags = BIND_UNIFORM_BUFFER;
        buffer.CPUAccessFlags = CPU_ACCESS_WRITE;
        RefCntAutoPtr<IBuffer> candidate;
        device->CreateBuffer(buffer, nullptr, &candidate);
        if (!candidate)
            throw std::runtime_error("UI filter constant allocation failed");
        const ShaderResourceVariableDesc variables[] = {
            {SHADER_TYPE_PIXEL, "input_image", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            {SHADER_TYPE_PIXEL, "mask_image", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
        SamplerDesc sampler;
        sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
        const ImmutableSamplerDesc immutable[] = {{SHADER_TYPE_PIXEL, "input_image", sampler},
                                                  {SHADER_TYPE_PIXEL, "mask_image", sampler}};
        for (unsigned f = 0; f < 2; ++f)
            for (unsigned mode = 0; mode < 4; ++mode) {
                GraphicsPipelineStateCreateInfo p;
                p.PSODesc.Name = "FORGE UI layer composition";
                p.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
                p.pVS = vs;
                p.pPS = ps;
                auto& graphics = p.GraphicsPipeline;
                graphics.NumRenderTargets = 1;
                graphics.RTVFormats[0] = f ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
                graphics.DSVFormat = TEX_FORMAT_D24_UNORM_S8_UINT;
                graphics.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                graphics.RasterizerDesc.CullMode = CULL_MODE_NONE;
                graphics.RasterizerDesc.ScissorEnable = True;
                auto& depth = graphics.DepthStencilDesc;
                depth.DepthEnable = depth.DepthWriteEnable = False;
                depth.StencilEnable = mode >= 2;
                depth.StencilReadMask = 255;
                depth.StencilWriteMask = 0;
                depth.FrontFace.StencilFunc = COMPARISON_FUNC_EQUAL;
                depth.FrontFace.StencilPassOp = STENCIL_OP_KEEP;
                depth.BackFace = depth.FrontFace;
                auto& blend = graphics.BlendDesc.RenderTargets[0];
                blend.BlendEnable = (mode & 1) != 0;
                blend.SrcBlend = blend.SrcBlendAlpha = BLEND_FACTOR_ONE;
                blend.DestBlend = blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
                p.PSODesc.ResourceLayout.Variables = variables;
                p.PSODesc.ResourceLayout.NumVariables = 2;
                p.PSODesc.ResourceLayout.ImmutableSamplers = immutable;
                p.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
                device->CreateGraphicsPipelineState(p, &pipelines[f][mode]);
                if (!pipelines[f][mode])
                    throw std::runtime_error("UI filter pipeline creation failed");
                pipelines[f][mode]
                    ->GetStaticVariableByName(SHADER_TYPE_PIXEL, "UiFilterData")
                    ->Set(candidate);
            }
        pipelines[0][0]->CreateShaderResourceBinding(&base_binding, true);
        if (!base_binding)
            throw std::runtime_error("UI filter binding allocation failed");
        constants = std::move(candidate);
    }
    void discard(Image& image) {
        bytes -= image.bytes;
        image = {};
    }
    std::size_t acquire() {
        const auto wanted = format ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
        const auto size = std::uint64_t(width) * height * (format ? 8 : 4);
        if (size > image_budget)
            throw std::runtime_error("UI filter image exceeds its 256 MiB logical budget");
        for (std::size_t i = 0; i < images.size(); ++i)
            if (const auto& d = images[i].texture ? images[i].texture->GetDesc() : TextureDesc{};
                !images[i].active && !images[i].pins && images[i].texture && d.Width == width &&
                d.Height == height && d.Format == wanted) {
                images[i].active = true;
                images[i].used = ++use;
                return i;
            }
        while (size > image_budget - bytes) {
            auto oldest = images.end();
            for (auto at = images.begin(); at != images.end(); ++at)
                if (!at->active && !at->pins && at->texture &&
                    (oldest == images.end() || at->used < oldest->used))
                    oldest = at;
            if (oldest == images.end())
                throw std::runtime_error("UI filter layers exceed their 256 MiB logical budget");
            discard(*oldest);
        }
        auto slot = std::find_if(images.begin(), images.end(),
                                 [](const auto& image) { return !image.texture; });
        if (slot == images.end() && images.size() >= max_images) {
            for (auto at = images.begin(); at != images.end(); ++at)
                if (!at->active && !at->pins && (slot == images.end() || at->used < slot->used))
                    slot = at;
            if (slot == images.end())
                throw std::runtime_error("UI filter scratch image count exceeded");
            discard(*slot);
        }
        const auto index =
            slot == images.end() ? images.size() : std::size_t(slot - images.begin());
        if (index == images.size())
            images.emplace_back();
        TextureDesc desc;
        desc.Name = "FORGE UI filter layer";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = wanted;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        Image candidate;
        device->CreateTexture(desc, nullptr, &candidate.texture);
        ++allocations;
        if (!candidate.texture)
            throw std::runtime_error("UI filter layer allocation failed");
        candidate.bytes = size;
        candidate.used = ++use;
        candidate.active = true;
        images[index] = std::move(candidate);
        bytes += size;
        return index;
    }
    ITextureView* image(Rml::LayerHandle handle, TEXTURE_VIEW_TYPE type) const {
        auto* texture = handle ? images.at(layers.at(handle)).texture.RawPtr() : base->GetTexture();
        auto* view = texture->GetDefaultView(type);
        if (!view)
            throw std::runtime_error("UI filter source or destination view unavailable");
        return view;
    }
    void pass(ITextureView* input, ITextureView* output, const Filter& filter, unsigned direction,
              Rml::BlendMode blend, ITextureView* stencil, unsigned stencil_ref, bool clip,
              bool scissor, Rml::Rectanglei region, ITextureView* mask = nullptr) {
        context->SetRenderTargets(1, &output, stencil, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport;
        viewport.Width = float(width);
        viewport.Height = float(height);
        context->SetViewports(1, &viewport, width, height);
        Rect rect{0, 0, Int32(width), Int32(height)};
        if (scissor) {
            rect.left = std::clamp(region.Left(), 0, int(width));
            rect.top = std::clamp(region.Top(), 0, int(height));
            rect.right = std::clamp(region.Right(), rect.left, int(width));
            rect.bottom = std::clamp(region.Bottom(), rect.top, int(height));
        }
        context->SetScissorRects(1, &rect, width, height);
        context->SetStencilRef(stencil_ref);
        const unsigned mode = (clip ? 2 : 0) + (blend == Rml::BlendMode::Blend ? 1 : 0);
        context->SetPipelineState(pipelines[format][mode]);
        {
            MapHelper<Constants> cb(context, constants, MAP_WRITE, MAP_FLAG_DISCARD);
            *cb = {};
            cb->viewport[0] = float(width);
            cb->viewport[1] = float(height);
            cb->viewport[2] = float(format);
            cb->filter[0] = float(filter.kind);
            cb->filter[1] = filter.value;
            cb->filter[2] = float(filter.radius);
            cb->filter[3] = float(direction);
            std::copy(filter.offset.begin(), filter.offset.end(), cb->offset);
            std::copy(filter.color.begin(), filter.color.end(), cb->color);
            std::memcpy(cb->weights, filter.weights.data(), sizeof(cb->weights));
        }
        base_binding->GetVariableByName(SHADER_TYPE_PIXEL, "input_image")->Set(input);
        base_binding->GetVariableByName(SHADER_TYPE_PIXEL, "mask_image")->Set(mask ? mask : input);
        context->CommitShaderResources(base_binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs(3, DRAW_FLAG_VERIFY_ALL));
    }
};
UiFilterLayers::UiFilterLayers(IRenderDevice* device) : impl_(std::make_unique<Impl>(device)) {}
UiFilterLayers::~UiFilterLayers() = default;
void UiFilterLayers::begin(IDeviceContext* context, ITextureView* base, unsigned width,
                           unsigned height) {
    end();
    auto& s = *impl_;
    ++s.frame;
    s.context = context;
    s.base = base;
    s.width = width;
    s.height = height;
    s.format = base->GetDesc().Format == TEX_FORMAT_RGBA16_FLOAT ? 1u : 0u;
}
void UiFilterLayers::end() {
    auto& s = *impl_;
    for (auto& image : s.images)
        image.active = false;
    if (s.base_binding) {
        s.base_binding->GetVariableByName(SHADER_TYPE_PIXEL, "input_image")->Set(nullptr);
        s.base_binding->GetVariableByName(SHADER_TYPE_PIXEL, "mask_image")->Set(nullptr);
    }
    s.stack.clear();
    s.layers.clear();
    s.context.Release();
    s.base.Release();
}
Rml::CompiledFilterHandle UiFilterLayers::compile(const Rml::String& name,
                                                  const Rml::Dictionary& parameters) {
    auto& s = *impl_;
    const auto filter = admit_filter(name, parameters);
    if (s.filters.size() >= max_filters)
        throw std::runtime_error("UI compiled filter count exceeds 256");
    s.pipelines_ready();
    const auto handle = s.next++;
    s.filters.emplace(handle, Impl::CompiledFilter{filter, {}, 0});
    return handle;
}
void UiFilterLayers::release(Rml::CompiledFilterHandle filter) {
    auto& s = *impl_;
    const auto found = s.filters.find(filter);
    if (found == s.filters.end())
        return;
    if (found->second.mask)
        --s.images.at(*found->second.mask).pins;
    s.filters.erase(found);
}
Rml::CompiledFilterHandle UiFilterLayers::save_mask() {
    auto& s = *impl_;
    if (!s.context || !s.base)
        throw std::runtime_error("UI mask capture requires an active UI frame");
    if (s.filters.size() >= max_filters)
        throw std::runtime_error("UI compiled filter count exceeds 256");
    s.pipelines_ready();
    const auto slot = s.acquire();
    try {
        // Snapshot isolation: subsequent drawing to the original layer cannot
        // change this alpha image. The existing pool owns and budgets the copy.
        s.pass(s.image(s.stack.empty() ? 0 : s.stack.back(), TEXTURE_VIEW_SHADER_RESOURCE),
               s.images[slot].texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), Filter{}, 0,
               Rml::BlendMode::Replace, nullptr, 0, false, false, {});
        Filter effect;
        effect.kind = Kind::AlphaMask;
        const auto handle = s.next++;
        s.filters.emplace(handle, Impl::CompiledFilter{effect, slot, s.frame});
        ++s.images[slot].pins;
        s.images[slot].active = false;
        return handle;
    } catch (...) {
        s.images[slot].active = false;
        throw;
    }
}
Rml::LayerHandle UiFilterLayers::push() {
    auto& s = *impl_;
    if (!s.context || s.stack.size() >= max_layers)
        throw std::runtime_error("UI filter layer stack exceeds four active layers");
    s.pipelines_ready();
    const auto slot = s.acquire();
    const auto handle = s.next++;
    try {
        s.layers.emplace(handle, slot);
        s.stack.push_back(handle);
    } catch (...) {
        s.layers.erase(handle);
        s.images[slot].active = false;
        throw;
    }
    const float clear[4]{};
    s.context->ClearRenderTarget(s.images[slot].texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                                 clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    return handle;
}
void UiFilterLayers::pop() {
    auto& s = *impl_;
    if (s.stack.empty())
        throw std::runtime_error("Unbalanced UI filter layer stack");
    const auto handle = s.stack.back();
    s.images.at(s.layers.at(handle)).active = false;
    s.layers.erase(handle);
    s.stack.pop_back();
}
ITextureView* UiFilterLayers::current_color() const {
    const auto& s = *impl_;
    return s.base ? s.image(s.stack.empty() ? 0 : s.stack.back(), TEXTURE_VIEW_RENDER_TARGET)
                  : nullptr;
}
void UiFilterLayers::composite(Rml::LayerHandle source, Rml::LayerHandle destination,
                               Rml::BlendMode blend,
                               Rml::Span<const Rml::CompiledFilterHandle> filters,
                               ITextureView* stencil, unsigned stencil_ref, bool clip, bool scissor,
                               Rml::Rectanglei region) {
    auto& s = *impl_;
    if (!s.context || filters.size() > max_chain ||
        (blend != Rml::BlendMode::Blend && blend != Rml::BlendMode::Replace))
        throw std::runtime_error("Invalid UI filter composition or chain exceeds eight");
    s.pipelines_ready();
    auto* input = s.image(source, TEXTURE_VIEW_SHADER_RESOURCE);
    auto* output = s.image(destination, TEXTURE_VIEW_RENDER_TARGET);
    std::optional<std::size_t> temporary, writing, retained;
    const auto release = [&] {
        if (temporary)
            s.images[*temporary].active = false;
        if (writing)
            s.images[*writing].active = false;
        if (retained)
            s.images[*retained].active = false;
    };
    try {
        const auto run = [&](const Filter& filter, unsigned direction,
                             ITextureView* mask = nullptr) {
            writing = s.acquire();
            auto& image = s.images[*writing];
            s.pass(input, image.texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET), filter,
                   direction, Rml::BlendMode::Replace, stencil, 0, false, false, {}, mask);
            input = image.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            if (temporary)
                s.images[*temporary].active = false;
            temporary = writing;
            writing.reset();
        };
        for (const auto handle : filters) {
            const auto filter = s.filters.find(handle);
            if (filter == s.filters.end())
                throw std::runtime_error("UI filter handle is stale");
            if (filter->second.mask) {
                if (filter->second.frame != s.frame)
                    throw std::runtime_error("UI mask filter belongs to an earlier frame");
                auto* mask = s.images.at(*filter->second.mask)
                                 .texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                run(filter->second.effect, 0, mask);
            } else if (filter->second.effect.kind == Kind::DropShadow) {
                if (filter->second.effect.color[3] == 0)
                    continue;
                // Preserve the incoming chain image until it is composited over
                // its blurred, displaced alpha silhouette. The pool owns every
                // intermediate; keeping it active also prevents target aliasing.
                auto* original = input;
                retained = std::exchange(temporary, {});
                auto blur = filter->second.effect;
                blur.kind = Kind::Blur;
                if (blur.radius) {
                    run(blur, 0);
                    run(blur, 1);
                }
                run(filter->second.effect, 0);
                s.pass(original,
                       s.images[*temporary].texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                       Filter{}, 0, Rml::BlendMode::Blend, stencil, 0, false, false, {});
                if (retained)
                    s.images[*retained].active = false;
                retained.reset();
            } else if (filter->second.effect.kind == Kind::Blur) {
                if (filter->second.effect.radius) {
                    run(filter->second.effect, 0);
                    run(filter->second.effect, 1);
                }
            } else {
                run(filter->second.effect, 0);
            }
        }
        if (input->GetTexture() == output->GetTexture())
            run(Filter{}, 0);
        s.pass(input, output, Filter{}, 0, blend, stencil, stencil_ref, clip, scissor, region);
    } catch (...) {
        release();
        throw;
    }
    release();
}
UiFilterLayers::Statistics UiFilterLayers::statistics() const {
    const auto& s = *impl_;
    const auto count = std::count_if(s.images.begin(), s.images.end(),
                                     [](const auto& image) { return bool(image.texture); });
    return {s.allocations, s.bytes, unsigned(count), unsigned(s.filters.size()),
            unsigned(s.stack.size())};
}
} // namespace forge
