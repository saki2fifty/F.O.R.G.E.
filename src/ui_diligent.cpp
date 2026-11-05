#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Sampler.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "ui_filter_layers.hpp"
#include "ui_gradient_admission.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <map>
#include <stdexcept>
using namespace Diligent;
namespace forge {
struct UiDiligentRenderer::Impl final : Rml::RenderInterface {
    struct Geometry {
        RefCntAutoPtr<IBuffer> vertices, indices;
        Uint32 count;
        std::size_t bytes;
    };
    struct Texture {
        RefCntAutoPtr<ITexture> texture;
        RefCntAutoPtr<IShaderResourceBinding> bindings;
        std::size_t bytes = 0;
        std::uintptr_t snapshot = 0;
        bool linear = false;
    };
    struct Constants {
        float matrix[16];
        float screen[4];
        float translation[4];
    };
    static_assert(ui_gradient_detail::max_stops == 16);
    struct GradientConstants {
        float line[4]{};
        float info[4]{};
        float positions[16]{};
        float colors[64]{};
    };
    UiFilterLayers layers;
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<ITexture> stencil;
    RefCntAutoPtr<IBuffer> constants;
    RefCntAutoPtr<IBuffer> gradient_constants;
    RefCntAutoPtr<ISampler> image_sampler, snapshot_sampler;
    std::array<std::array<RefCntAutoPtr<IPipelineState>, 4>, 2> pipelines;
    unsigned output_format = 0;
    std::uint64_t stencil_allocations = 0;
    struct ClippingImage {
        unsigned width, height;
        std::uint64_t used;
        RefCntAutoPtr<ITexture> image;
        std::uint64_t bytes() const { return std::uint64_t(width) * height * 4; }
    };
    std::vector<ClippingImage> clipping_images;
    std::uint64_t clipping_bytes = 0, clipping_use = 0;
    void clipping(unsigned w, unsigned h) {
        for (auto& entry : clipping_images)
            if (entry.width == w && entry.height == h) {
                entry.used = ++clipping_use;
                stencil = entry.image;
                return;
            }
        // Scratch clipping images belong to this renderer. Bound size churn by
        // count and logical D24S8 bytes; camera image ownership stays elsewhere.
        constexpr std::uint64_t budget = 256ull * 1024 * 1024;
        const auto bytes = std::uint64_t(w) * h * 4;
        stencil.Release();
        while (!clipping_images.empty() &&
               (clipping_images.size() >= 5 || clipping_bytes + bytes > budget)) {
            const auto oldest =
                std::min_element(clipping_images.begin(), clipping_images.end(),
                                 [](const auto& a, const auto& b) { return a.used < b.used; });
            clipping_bytes -= oldest->bytes();
            clipping_images.erase(oldest);
        }
        TextureDesc desc;
        desc.Name = "FORGE UI clipping";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = w;
        desc.Height = h;
        desc.Format = TEX_FORMAT_D24_UNORM_S8_UINT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        RefCntAutoPtr<ITexture> image;
        device->CreateTexture(desc, nullptr, &image);
        ++stencil_allocations;
        if (!image)
            throw std::runtime_error("UI stencil allocation failed");
        clipping_images.push_back({w, h, ++clipping_use, image});
        clipping_bytes += bytes;
        stencil = std::move(image);
    }
    std::map<Rml::CompiledGeometryHandle, Geometry> geometry;
    std::map<Rml::TextureHandle, Texture> textures;
    std::map<Rml::CompiledShaderHandle, ui_gradient_detail::Gradient> gradients;
    std::size_t geometry_bytes = 0, texture_bytes = 0;
    std::uintptr_t next = 1;
    Rml::TextureHandle white = 0;
    unsigned width = 0, height = 0, stencil_ref = 0;
    bool scissor = false, clip = false;
    Rml::Rectanglei region;
    std::array<float, 16> transform{};
    Impl(IRenderDevice* d) : layers(d), device(d) {
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        shader.Desc.UseCombinedTextureSamplers = True;
        shader.Desc.Name = "FORGE runtime UI VS";
        shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shader.Source =
            R"(cbuffer UiData {column_major float4x4 transform;float4 screen;float4 translation;};
struct Out {float4 p:SV_POSITION;float4 color:COLOR0;float2 uv:TEXCOORD0;nointerpolation float2 flags:TEXCOORD1;};
Out main(float2 v:ATTRIB0,float4 color:ATTRIB1,float2 uv:ATTRIB2){Out o;float4 p=mul(transform,float4(v+translation.xy,0,1));o.p=float4(2*p.x/screen.x-p.w,p.w-2*p.y/screen.y,0,p.w);o.color=color;o.uv=uv;o.flags=screen.zw;return o;})";
        RefCntAutoPtr<IShader> vs, ps;
        device->CreateShader(shader, &vs);
        shader.Desc.Name = "FORGE runtime UI PS";
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        // Reuse the pinned graphics stack's transfer function. RmlUi colors and
        // images arrive premultiplied in display space; decode straight RGB and
        // premultiply again for blending into a linear camera image.
        shader.Source = R"(#include "SRGBUtilities.fxh"
Texture2D ui_texture;SamplerState ui_texture_sampler;
cbuffer UiGradientData {float4 gradient_line;float4 gradient_info;
float4 gradient_positions[4];float4 gradient_colors[16];};
float stop_position(int i) {return gradient_positions[i/4][i%4];}
float4 gradient_color(float2 uv,float kind) {
    float t=dot(uv-gradient_line.xy,gradient_line.zw);
    if(kind>2.5) {
        float2 delta=uv-gradient_line.xy;
        t=dot(delta,delta)>0 ? frac(atan2(delta.x,-delta.y)*0.15915494309189535-gradient_line.z) : 0;
    } else if(kind>1.5) {
        t=length((uv-gradient_line.xy)*gradient_line.zw);
    }
    int count=clamp(int(gradient_info.x),2,16);
    if(gradient_info.y>0.5) {
        float period=gradient_info.w-gradient_info.z;
        t=gradient_info.z+frac((t-gradient_info.z)/period)*period;
    }
    if(t<stop_position(0)) return gradient_colors[0];
    for(int i=1;i<count;++i) {
        float right=stop_position(i);
        if(t<right) {
            float left=stop_position(i-1);
            return lerp(gradient_colors[i-1],gradient_colors[i],saturate((t-left)/(right-left)));
        }
    }
    return gradient_colors[count-1];
}
float4 main(float4 p:SV_POSITION,float4 color:COLOR0,float2 uv:TEXCOORD0,
            nointerpolation float2 flags:TEXCOORD1):SV_TARGET {
    float4 sample=flags.y>0.5 ? gradient_color(uv,flags.y) : ui_texture.Sample(ui_texture_sampler,uv);
    if(flags.y<-.5) {
        if(flags.x>0.5)
            color.rgb=color.a>0 ? SRGBToLinear(color.rgb/color.a)*color.a : float3(0,0,0);
        else
            sample.rgb=sample.a>0 ? LinearToSRGB(sample.rgb/sample.a)*sample.a : float3(0,0,0);
        return color*sample;
    }
    float4 value=color*sample;
    if(flags.x>0.5)
        value.rgb=value.a>0 ? SRGBToLinear(value.rgb/value.a)*value.a : float3(0,0,0);
    return value;
})";
        device->CreateShader(shader, &ps);
        if (!vs || !ps)
            throw std::runtime_error("Runtime UI shader compilation failed");
        BufferDesc cb;
        cb.Name = "FORGE UI constants";
        cb.Size = sizeof(Constants);
        cb.Usage = USAGE_DYNAMIC;
        cb.BindFlags = BIND_UNIFORM_BUFFER;
        cb.CPUAccessFlags = CPU_ACCESS_WRITE;
        device->CreateBuffer(cb, nullptr, &constants);
        if (!constants)
            throw std::runtime_error("Runtime UI constant allocation failed");
        cb.Name = "FORGE UI gradient constants";
        cb.Size = sizeof(GradientConstants);
        device->CreateBuffer(cb, nullptr, &gradient_constants);
        if (!gradient_constants)
            throw std::runtime_error("Runtime UI gradient constant allocation failed");
        LayoutElement layout[] = {
            {0, 0, 2, VT_FLOAT32, False}, {1, 0, 4, VT_UINT8, True}, {2, 0, 2, VT_FLOAT32, False}};
        static_assert(sizeof(Rml::Vertex) == 20);
        ShaderResourceVariableDesc variable{SHADER_TYPE_PIXEL, "ui_texture",
                                            SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
        SamplerDesc sampler;
        sampler.MinFilter = FILTER_TYPE_LINEAR;
        sampler.MagFilter = FILTER_TYPE_LINEAR;
        sampler.MipFilter = FILTER_TYPE_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_WRAP;
        device->CreateSampler(sampler, &image_sampler);
        sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
        device->CreateSampler(sampler, &snapshot_sampler);
        if (!image_sampler || !snapshot_sampler)
            throw std::runtime_error("Runtime UI sampler allocation failed");
        for (unsigned format = 0; format < pipelines.size(); ++format) {
            for (unsigned mode = 0; mode < 4; ++mode) {
                GraphicsPipelineStateCreateInfo p;
                p.PSODesc.Name = "FORGE runtime UI";
                p.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
                p.pVS = vs;
                p.pPS = ps;
                p.GraphicsPipeline.NumRenderTargets = 1;
                p.GraphicsPipeline.RTVFormats[0] =
                    format ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
                p.GraphicsPipeline.DSVFormat = TEX_FORMAT_D24_UNORM_S8_UINT;
                p.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                p.GraphicsPipeline.InputLayout.LayoutElements = layout;
                p.GraphicsPipeline.InputLayout.NumElements = 3;
                auto& raster = p.GraphicsPipeline.RasterizerDesc;
                raster.CullMode = CULL_MODE_NONE;
                raster.ScissorEnable = True;
                auto& depth = p.GraphicsPipeline.DepthStencilDesc;
                depth.DepthEnable = False;
                depth.DepthWriteEnable = False;
                depth.StencilEnable = mode != 0;
                depth.StencilReadMask = 255;
                depth.StencilWriteMask = mode >= 2 ? 255 : 0;
                depth.FrontFace.StencilFunc =
                    mode == 2 ? COMPARISON_FUNC_ALWAYS : COMPARISON_FUNC_EQUAL;
                depth.FrontFace.StencilPassOp = mode == 2   ? STENCIL_OP_REPLACE
                                                : mode == 3 ? STENCIL_OP_INCR_SAT
                                                            : STENCIL_OP_KEEP;
                depth.BackFace = depth.FrontFace;
                auto& blend = p.GraphicsPipeline.BlendDesc.RenderTargets[0];
                blend.BlendEnable = True;
                blend.SrcBlend = BLEND_FACTOR_ONE;
                blend.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
                blend.SrcBlendAlpha = BLEND_FACTOR_ONE;
                blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
                blend.RenderTargetWriteMask = mode >= 2 ? COLOR_MASK_NONE : COLOR_MASK_ALL;
                p.PSODesc.ResourceLayout.Variables = &variable;
                p.PSODesc.ResourceLayout.NumVariables = 1;
                device->CreateGraphicsPipelineState(p, &pipelines[format][mode]);
                if (!pipelines[format][mode])
                    throw std::runtime_error("Runtime UI pipeline creation failed");
                pipelines[format][mode]
                    ->GetStaticVariableByName(SHADER_TYPE_VERTEX, "UiData")
                    ->Set(constants);
                pipelines[format][mode]
                    ->GetStaticVariableByName(SHADER_TYPE_PIXEL, "UiGradientData")
                    ->Set(gradient_constants);
            }
        }
        SetTransform(nullptr);
        Rml::byte pixel[] = {255, 255, 255, 255};
        white = GenerateTexture({pixel, 4}, {1, 1});
    }
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> v,
                                                Rml::Span<const int> i) override {
        if (v.empty() || i.empty() || v.size() > 1000000 || i.size() > 3000000 || i.size() % 3 ||
            geometry.size() >= 8192)
            return 0;
        const auto bytes = v.size() * sizeof(Rml::Vertex) + i.size() * sizeof(int);
        if (bytes > 64 * 1024 * 1024 - geometry_bytes)
            return 0;
        for (auto index : i)
            if (index < 0 || std::size_t(index) >= v.size())
                return 0;
        Geometry g{};
        g.count = Uint32(i.size());
        g.bytes = bytes;
        auto make = [&](auto data, BIND_FLAGS bind, RefCntAutoPtr<IBuffer>& out) {
            BufferDesc desc;
            desc.Name = "FORGE UI geometry";
            desc.Usage = USAGE_IMMUTABLE;
            desc.BindFlags = bind;
            desc.Size = data.size() * sizeof(data[0]);
            BufferData initial;
            initial.pData = data.data();
            initial.DataSize = desc.Size;
            device->CreateBuffer(desc, &initial, &out);
        };
        make(v, BIND_VERTEX_BUFFER, g.vertices);
        make(i, BIND_INDEX_BUFFER, g.indices);
        if (!g.vertices || !g.indices)
            return 0;
        const auto id = next++;
        geometry.emplace(id, std::move(g));
        geometry_bytes += bytes;
        return id;
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle h) override {
        auto it = geometry.find(h);
        if (it != geometry.end()) {
            geometry_bytes -= it->second.bytes;
            geometry.erase(it);
        }
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> data,
                                       Rml::Vector2i size) override {
        if (size.x < 1 || size.y < 1 || size.x > 2048 || size.y > 2048 || textures.size() >= 512 ||
            data.size() != std::size_t(size.x) * size.y * 4 ||
            data.size() > 64 * 1024 * 1024 - texture_bytes)
            return 0;
        Texture t{};
        t.bytes = data.size();
        TextureDesc desc;
        desc.Name = "FORGE UI texture";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = size.x;
        desc.Height = size.y;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_SHADER_RESOURCE;
        desc.Usage = USAGE_IMMUTABLE;
        TextureSubResData sub;
        sub.pData = data.data();
        sub.Stride = size.x * 4;
        TextureData initial;
        initial.pSubResources = &sub;
        initial.NumSubresources = 1;
        device->CreateTexture(desc, &initial, &t.texture);
        if (!t.texture)
            return 0;
        t.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(image_sampler);
        pipelines[0][0]->CreateShaderResourceBinding(&t.bindings, true);
        t.bindings->GetVariableByName(SHADER_TYPE_PIXEL, "ui_texture")
            ->Set(t.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        const auto id = next++;
        textures.emplace(id, std::move(t));
        texture_bytes += data.size();
        return id;
    }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        Rml::String data;
        if (!Rml::GetFileInterface()->LoadFile(source, data))
            return 0;
        try {
            auto image =
                decode_ui_image({reinterpret_cast<const std::byte*>(data.data()), data.size()});
            dimensions = {int(image.width), int(image.height)};
            return GenerateTexture(
                {reinterpret_cast<const Rml::byte*>(image.rgba.data()), image.rgba.size()},
                dimensions);
        } catch (const std::exception& e) {
            Rml::Log::Message(Rml::Log::LT_ERROR, "%s", e.what());
            return 0;
        }
    }
    void ReleaseTexture(Rml::TextureHandle h) override {
        auto it = textures.find(h);
        if (it != textures.end()) {
            texture_bytes -= it->second.bytes;
            if (it->second.snapshot)
                layers.release_texture(it->second.snapshot);
            textures.erase(it);
        }
    }
    void bind_current() {
        auto* target = layers.current_color();
        if (!context || !target)
            return;
        context->SetRenderTargets(1, &target, stencil->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport view;
        view.Width = float(width);
        view.Height = float(height);
        context->SetViewports(1, &view, width, height);
    }
    Rml::CompiledFilterHandle CompileFilter(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override {
        return layers.compile(name, parameters);
    }
    void ReleaseFilter(Rml::CompiledFilterHandle filter) override { layers.release(filter); }
    Rml::TextureHandle SaveLayerAsTexture() override {
        if (!scissor || textures.size() >= 512)
            throw std::runtime_error(
                "UI texture snapshot requires scissor and a free texture handle");
        const auto saved = layers.save_texture(region);
        try {
            Texture item{};
            item.texture = saved.texture;
            item.snapshot = saved.token;
            item.linear = saved.texture->GetDesc().Format == TEX_FORMAT_RGBA16_FLOAT;
            auto* view = item.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            view->SetSampler(snapshot_sampler);
            pipelines[0][0]->CreateShaderResourceBinding(&item.bindings, true);
            if (!item.bindings)
                throw std::runtime_error("UI snapshot binding allocation failed");
            item.bindings->GetVariableByName(SHADER_TYPE_PIXEL, "ui_texture")->Set(view);
            const auto handle = next++;
            textures.emplace(handle, std::move(item));
            bind_current();
            return handle;
        } catch (...) {
            layers.release_texture(saved.token);
            bind_current();
            throw;
        }
    }
    Rml::CompiledFilterHandle SaveLayerAsMaskImage() override {
        const auto handle = layers.save_mask();
        bind_current();
        return handle;
    }
    Rml::CompiledShaderHandle CompileShader(const Rml::String& name,
                                            const Rml::Dictionary& parameters) override {
        const auto gradient = ui_gradient_detail::admit_gradient(name, parameters);
        if (gradients.size() >= ui_gradient_detail::max_shaders)
            throw std::runtime_error("UI compiled gradient shader count exceeds 256");
        const auto handle = next++;
        gradients.emplace(handle, gradient);
        return handle;
    }
    void ReleaseShader(Rml::CompiledShaderHandle shader) override { gradients.erase(shader); }
    void RenderShader(Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
                      Rml::Vector2f translation, Rml::TextureHandle texture) override {
        const auto found = gradients.find(shader);
        if (found == gradients.end() || texture)
            throw std::runtime_error("UI gradient requires a live untextured shader handle");
        draw(geometry, translation, 0, clip ? 1 : 0, &found->second);
    }
    Rml::LayerHandle PushLayer() override {
        const auto handle = layers.push();
        bind_current();
        return handle;
    }
    void PopLayer() override {
        layers.pop();
        bind_current();
    }
    void CompositeLayers(Rml::LayerHandle source, Rml::LayerHandle destination,
                         Rml::BlendMode blend,
                         Rml::Span<const Rml::CompiledFilterHandle> filters) override {
        try {
            layers.composite(source, destination, blend, filters,
                             stencil->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), stencil_ref, clip,
                             scissor, region);
        } catch (...) {
            bind_current();
            throw;
        }
        bind_current();
    }
    void EnableScissorRegion(bool value) override { scissor = value; }
    void SetScissorRegion(Rml::Rectanglei value) override { region = value; }
    void SetTransform(const Rml::Matrix4f* m) override {
        if (m)
            std::memcpy(transform.data(), m->data(), sizeof(float) * 16);
        else {
            transform.fill(0);
            for (unsigned i = 0; i < 4; ++i)
                transform[i * 5] = 1;
        }
    }
    void EnableClipMask(bool enabled) override { clip = enabled; }
    void draw(Rml::CompiledGeometryHandle h, Rml::Vector2f offset, Rml::TextureHandle tex,
              unsigned mode, const ui_gradient_detail::Gradient* gradient = nullptr) {
        if (!context)
            return;
        auto g = geometry.find(h);
        auto t = textures.find(tex ? tex : white);
        if (g == geometry.end() || t == textures.end())
            return;
        context->SetPipelineState(pipelines[output_format][mode]);
        {
            MapHelper<Constants> cb(context, constants, MAP_WRITE, MAP_FLAG_DISCARD);
            std::memcpy(cb->matrix, transform.data(), 64);
            cb->screen[0] = float(width);
            cb->screen[1] = float(height);
            cb->screen[2] = float(output_format);
            cb->screen[3] = gradient ? float(gradient->kind) : t->second.linear ? -1.f : 0.f;
            cb->translation[0] = offset.x;
            cb->translation[1] = offset.y;
            cb->translation[2] = cb->translation[3] = 0;
        }
        if (gradient) {
            MapHelper<GradientConstants> cb(context, gradient_constants, MAP_WRITE,
                                            MAP_FLAG_DISCARD);
            std::copy(gradient->line.begin(), gradient->line.end(), cb->line);
            cb->info[0] = float(gradient->count);
            cb->info[1] = gradient->repeating ? 1.f : 0.f;
            cb->info[2] = gradient->positions[0];
            cb->info[3] = gradient->positions[gradient->count - 1];
            std::memcpy(cb->positions, gradient->positions.data(), sizeof(cb->positions));
            std::memcpy(cb->colors, gradient->colors.data(), sizeof(cb->colors));
        }
        Rect rect{0, 0, Int32(width), Int32(height)};
        if (scissor) {
            rect.left = std::clamp(region.Left(), 0, int(width));
            rect.top = std::clamp(region.Top(), 0, int(height));
            rect.right = std::clamp(region.Right(), rect.left, int(width));
            rect.bottom = std::clamp(region.Bottom(), rect.top, int(height));
        }
        context->SetScissorRects(1, &rect, width, height);
        context->SetStencilRef(stencil_ref);
        IBuffer* vb = g->second.vertices;
        Uint64 start = 0;
        context->SetVertexBuffers(0, 1, &vb, &start, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                  SET_VERTEX_BUFFERS_FLAG_RESET);
        context->SetIndexBuffer(g->second.indices, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->CommitShaderResources(t->second.bindings,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawIndexedAttribs draw;
        draw.NumIndices = g->second.count;
        draw.IndexType = VT_UINT32;
        draw.Flags = DRAW_FLAG_VERIFY_ALL;
        context->DrawIndexed(draw);
    }
    void RenderGeometry(Rml::CompiledGeometryHandle h, Rml::Vector2f p,
                        Rml::TextureHandle t) override {
        draw(h, p, t, clip ? 1 : 0);
    }
    void RenderToClipMask(Rml::ClipMaskOperation op, Rml::CompiledGeometryHandle h,
                          Rml::Vector2f p) override {
        if (!context)
            return;
        const bool old_scissor = scissor;
        scissor = false;
        if (op == Rml::ClipMaskOperation::Intersect) {
            if (stencil_ref >= 254)
                throw std::runtime_error("UI clip nesting limit");
            draw(h, p, 0, 3);
            ++stencil_ref;
        } else {
            context->ClearDepthStencil(stencil->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                       CLEAR_STENCIL_FLAG, 1,
                                       op == Rml::ClipMaskOperation::SetInverse ? 1 : 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            stencil_ref = op == Rml::ClipMaskOperation::SetInverse ? 0 : 1;
            draw(h, p, 0, 2);
            stencil_ref = 1;
        }
        scissor = old_scissor;
    }
};
UiDiligentRenderer::UiDiligentRenderer(IRenderDevice* d) : impl_(std::make_unique<Impl>(d)) {}
UiDiligentRenderer::~UiDiligentRenderer() = default;
Rml::RenderInterface& UiDiligentRenderer::render_interface() { return *impl_; }
void UiDiligentRenderer::begin(IDeviceContext* c, ITextureView* color, unsigned w, unsigned h) {
    auto& s = *impl_;
    if (!c || !color || !w || !h || w > 8192 || h > 8192)
        throw std::runtime_error("Invalid UI render target");
    auto* target = color->GetTexture()->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    if (!target || (target->GetDesc().Format != TEX_FORMAT_RGBA8_UNORM &&
                    target->GetDesc().Format != TEX_FORMAT_RGBA16_FLOAT))
        throw std::runtime_error("Runtime UI needs a display RGBA8 or linear HDR RGBA16 target");
    s.output_format = target->GetDesc().Format == TEX_FORMAT_RGBA16_FLOAT ? 1u : 0u;
    s.context = c;
    {
        // Keep this dynamic binding valid for ordinary UI frames as well. Only
        // gradient draws replace its copied row; ordinary geometry stays on the
        // existing small vertex constants and texture path.
        MapHelper<UiDiligentRenderer::Impl::GradientConstants> cb(c, s.gradient_constants,
                                                                  MAP_WRITE, MAP_FLAG_DISCARD);
        *cb = {};
        cb->info[0] = 2;
        cb->info[3] = cb->positions[1] = 1;
    }
    s.layers.begin(c, target, w, h);
    s.clipping(w, h);
    s.width = w;
    s.height = h;
    s.scissor = s.clip = false;
    s.stencil_ref = 0;
    s.SetTransform(nullptr);
    s.bind_current();
}
void UiDiligentRenderer::end() {
    impl_->layers.end();
    impl_->context.Release();
}
UiDiligentRenderer::FilterStats UiDiligentRenderer::filter_stats() const {
    const auto s = impl_->layers.statistics();
    return {s.allocations, s.cached_bytes, s.cached_images, s.filters, s.active_layers};
}
UiDiligentRenderer::ClippingStats UiDiligentRenderer::clipping_stats() const {
    const auto& s = *impl_;
    return {s.stencil_allocations, unsigned(s.clipping_images.size()), s.clipping_bytes};
}
void UiDiligentRenderer::draw_output(UiPresenter& presenter, AssetRef<RenderTextureAsset> output,
                                     IDeviceContext* context, ITextureView* color, unsigned width,
                                     unsigned height) {
    if (!presenter.has_output(output))
        return;
    try {
        // begin validates dimensions before conversion to the native layout type.
        begin(context, color, width, height);
        presenter.update_output(output, int(width), int(height));
        presenter.render_output(output);
    } catch (...) {
        end();
        throw;
    }
    end();
}
} // namespace forge
