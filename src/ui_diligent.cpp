#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
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
        std::size_t bytes;
    };
    struct Constants {
        float matrix[16];
        float screen[4];
        float translation[4];
    };
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<ITexture> stencil;
    RefCntAutoPtr<IBuffer> constants;
    std::array<RefCntAutoPtr<IPipelineState>, 4> pipelines;
    std::map<Rml::CompiledGeometryHandle, Geometry> geometry;
    std::map<Rml::TextureHandle, Texture> textures;
    std::size_t geometry_bytes = 0, texture_bytes = 0;
    std::uintptr_t next = 1;
    Rml::TextureHandle white = 0;
    unsigned width = 0, height = 0, stencil_ref = 0;
    bool scissor = false, clip = false;
    Rml::Rectanglei region;
    std::array<float, 16> transform{};
    Impl(IRenderDevice* d) : device(d) {
        ShaderCreateInfo shader;
        shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        shader.EntryPoint = "main";
        shader.Desc.UseCombinedTextureSamplers = True;
        shader.Desc.Name = "FORGE runtime UI VS";
        shader.Desc.ShaderType = SHADER_TYPE_VERTEX;
        shader.Source =
            R"(cbuffer UiData {column_major float4x4 transform;float4 screen;float4 translation;};
struct Out {float4 p:SV_POSITION;float4 color:COLOR0;float2 uv:TEXCOORD0;};
Out main(float2 v:ATTRIB0,float4 color:ATTRIB1,float2 uv:ATTRIB2){Out o;float4 p=mul(transform,float4(v+translation.xy,0,1));o.p=float4(2*p.x/screen.x-p.w,p.w-2*p.y/screen.y,0,p.w);o.color=color;o.uv=uv;return o;})";
        RefCntAutoPtr<IShader> vs, ps;
        device->CreateShader(shader, &vs);
        shader.Desc.Name = "FORGE runtime UI PS";
        shader.Desc.ShaderType = SHADER_TYPE_PIXEL;
        shader.Source = R"(Texture2D ui_texture;SamplerState ui_texture_sampler;
float4 main(float4 p:SV_POSITION,float4 color:COLOR0,float2 uv:TEXCOORD0):SV_TARGET{return color*ui_texture.Sample(ui_texture_sampler,uv);})";
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
        ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL, "ui_texture", sampler};
        for (unsigned mode = 0; mode < 4; ++mode) {
            GraphicsPipelineStateCreateInfo p;
            p.PSODesc.Name = "FORGE runtime UI";
            p.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
            p.pVS = vs;
            p.pPS = ps;
            p.GraphicsPipeline.NumRenderTargets = 1;
            p.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
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
            p.PSODesc.ResourceLayout.ImmutableSamplers = &immutable;
            p.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
            device->CreateGraphicsPipelineState(p, &pipelines[mode]);
            if (!pipelines[mode])
                throw std::runtime_error("Runtime UI pipeline creation failed");
            pipelines[mode]->GetStaticVariableByName(SHADER_TYPE_VERTEX, "UiData")->Set(constants);
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
        pipelines[0]->CreateShaderResourceBinding(&t.bindings, true);
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
            textures.erase(it);
        }
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
              unsigned mode) {
        if (!context)
            return;
        auto g = geometry.find(h);
        auto t = textures.find(tex ? tex : white);
        if (g == geometry.end() || t == textures.end())
            return;
        context->SetPipelineState(pipelines[mode]);
        {
            MapHelper<Constants> cb(context, constants, MAP_WRITE, MAP_FLAG_DISCARD);
            std::memcpy(cb->matrix, transform.data(), 64);
            cb->screen[0] = float(width);
            cb->screen[1] = float(height);
            cb->screen[2] = cb->screen[3] = 0;
            cb->translation[0] = offset.x;
            cb->translation[1] = offset.y;
            cb->translation[2] = cb->translation[3] = 0;
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
    s.context = c;
    if (!s.stencil || s.width != w || s.height != h) {
        TextureDesc desc;
        desc.Name = "FORGE UI clipping";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = w;
        desc.Height = h;
        desc.Format = TEX_FORMAT_D24_UNORM_S8_UINT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        s.stencil.Release();
        s.device->CreateTexture(desc, nullptr, &s.stencil);
        if (!s.stencil)
            throw std::runtime_error("UI stencil allocation failed");
    }
    s.width = w;
    s.height = h;
    s.scissor = s.clip = false;
    s.stencil_ref = 0;
    s.SetTransform(nullptr);
    auto* target = color->GetTexture()->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    c->SetRenderTargets(1, &target, s.stencil->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Viewport view;
    view.Width = float(w);
    view.Height = float(h);
    c->SetViewports(1, &view, w, h);
}
void UiDiligentRenderer::end() { impl_->context.Release(); }
} // namespace forge
