#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <RmlUi/Core.h>
#include <array>
#include <cmath>
#include <forge/ui_diligent.hpp>
#include <stdexcept>
#include <vector>

namespace forge::test_ui_filters {
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class Read8, class Read16>
void exercise(UiDiligentRenderer& renderer, Diligent::IRenderDevice* device,
              Diligent::IDeviceContext* context, Read8 read8, Read16 read16) {
    using namespace Diligent;
    auto& r = renderer.render_interface();
    const auto shape = [&](float x, float y, float w, float h, Rml::ColourbPremultiplied color) {
        const Rml::Vertex vertices[]{{{x, y}, color, {0, 0}},
                                     {{x + w, y}, color, {1, 0}},
                                     {{x + w, y + h}, color, {1, 1}},
                                     {{x, y + h}, color, {0, 1}}};
        const int indices[]{0, 1, 2, 0, 2, 3};
        const auto handle = r.CompileGeometry({vertices, 4}, {indices, 6});
        check(handle != 0, "Filter fixture geometry admission failed");
        return handle;
    };
    const auto rectangle = shape(16, 8, 32, 16, {128, 0, 0, 128});
    const auto middle = shape(16, 8, 32, 16, {64, 0, 0, 128});
    const auto mask = shape(24, 0, 16, 32, {255, 255, 255, 255});
    const auto impulse = shape(31, 15, 2, 2, {255, 255, 255, 255});
    const auto opacity = r.CompileFilter("opacity", {{"value", Rml::Variant(.5f)}});
    const auto blur = r.CompileFilter("blur", {{"sigma", Rml::Variant(1.f)}});
    const auto brightness = r.CompileFilter("brightness", {{"value", Rml::Variant(.5f)}});
    const auto contrast = r.CompileFilter("contrast", {{"value", Rml::Variant(0.f)}});
    const auto grayscale = r.CompileFilter("grayscale", {{"value", Rml::Variant(1.f)}});
    check(opacity && blur, "Filter metadata/shader admission failed");
    const std::array<Rml::CompiledFilterHandle, 1> opacity_chain{opacity}, blur_chain{blur};
    const auto target = [&](TEXTURE_FORMAT format, unsigned w = 64, unsigned h = 32) {
        TextureDesc desc;
        desc.Name = "FORGE UI filter fixture";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = w;
        desc.Height = h;
        desc.Format = format;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> image;
        device->CreateTexture(desc, nullptr, &image);
        check(bool(image), "Filter fixture target allocation failed");
        return image;
    };
    const auto begin = [&](ITexture* image, const std::array<float, 4>& clear) {
        auto* view = image->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &view, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(view, clear.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        renderer.begin(context, view, image->GetDesc().Width, image->GetDesc().Height);
    };
    auto color = target(TEX_FORMAT_RGBA8_UNORM);
    for (unsigned frame = 0; frame < 2; ++frame) {
        const auto before = renderer.filter_stats().allocations;
        begin(color, {0, 0, 1, 1});
        const auto layer = r.PushLayer();
        check(layer != 0, "Filter layer push failed");
        r.RenderGeometry(rectangle, {}, 0);
        r.CompositeLayers(layer, 0, Rml::BlendMode::Blend,
                          {opacity_chain.data(), opacity_chain.size()});
        r.PopLayer();
        renderer.end();
        const auto pixels = read8(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
        const auto center = pixels.at(16 * 64 + 32), outside = pixels.at(2 * 64 + 2);
        check(std::abs(int(center[0]) - 64) <= 1 && std::abs(int(center[2]) - 191) <= 1 &&
                  center[3] == 255 && outside == std::array<unsigned char, 4>{0, 0, 255, 255},
              "Filtered premultiplied color/alpha or transparent outside differs");
        if (frame)
            check(renderer.filter_stats().allocations == before,
                  "Repeated filtered frames allocate new layer images");
    }
    begin(color, {0, 0, 1, 1});
    const auto outer = r.PushLayer(), inner = r.PushLayer();
    r.RenderGeometry(rectangle, {}, 0);
    r.CompositeLayers(inner, outer, Rml::BlendMode::Blend,
                      {opacity_chain.data(), opacity_chain.size()});
    r.PopLayer();
    r.CompositeLayers(outer, 0, Rml::BlendMode::Blend,
                      {opacity_chain.data(), opacity_chain.size()});
    r.PopLayer();
    renderer.end();
    auto pixels = read8(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
    check(std::abs(int(pixels.at(16 * 64 + 32)[0]) - 32) <= 1 &&
              std::abs(int(pixels.at(16 * 64 + 32)[2]) - 223) <= 1,
          "Nested filtered layers lost their transparent parent composition");

    const auto filtered_center = [&](Rml::Span<const Rml::CompiledFilterHandle> chain) {
        begin(color, {0, 0, 1, 1});
        const auto layer = r.PushLayer();
        r.RenderGeometry(rectangle, {}, 0);
        r.CompositeLayers(layer, 0, Rml::BlendMode::Blend, chain);
        r.PopLayer();
        renderer.end();
        return read8(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)).at(16 * 64 + 32);
    };
    for (const auto& [filter, expected] : {std::pair{brightness, std::array{64, 0, 127, 255}},
                                           std::pair{contrast, std::array{64, 64, 191, 255}},
                                           std::pair{grayscale, std::array{27, 27, 154, 255}}}) {
        const auto value = filtered_center({&filter, 1});
        for (unsigned i = 0; i < 4; ++i)
            check(std::abs(int(value[i]) - expected[i]) <= 1,
                  "Color filter changed alpha or produced the wrong channel value");
    }
    const Rml::CompiledFilterHandle first[]{contrast, brightness}, second[]{brightness, contrast};
    check(filtered_center({first, 2}) != filtered_center({second, 2}),
          "UI filters lost authored chain order");

    begin(color, {0, 0, 0, 0});
    auto layer = r.PushLayer();
    r.RenderGeometry(impulse, {}, 0);
    r.CompositeLayers(layer, 0, Rml::BlendMode::Replace, {blur_chain.data(), blur_chain.size()});
    r.PopLayer();
    renderer.end();
    pixels = read8(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
    unsigned mass = 0, coverage = 0;
    for (const auto pixel : pixels) {
        mass += pixel[0];
        coverage += pixel[0] != 0;
        check(std::abs(int(pixel[0]) - pixel[3]) <= 1,
              "Blur did not preserve premultiplied white/alpha");
    }
    check(mass > 980 && mass < 1060 && coverage > 16 &&
              pixels.at(16 * 64 + 29) == pixels.at(16 * 64 + 34) &&
              pixels.at(13 * 64 + 32) == pixels.at(18 * 64 + 32),
          "Blur lost impulse energy, spread or axis symmetry");

    begin(color, {0, 0, 1, 1});
    r.RenderToClipMask(Rml::ClipMaskOperation::Set, mask, {});
    r.EnableClipMask(true);
    layer = r.PushLayer();
    r.RenderGeometry(rectangle, {}, 0);
    r.EnableScissorRegion(true);
    r.SetScissorRegion(Rml::Rectanglei::FromPositionSize({0, 0}, {36, 32}));
    r.CompositeLayers(layer, 0, Rml::BlendMode::Blend, {blur_chain.data(), blur_chain.size()});
    r.PopLayer();
    renderer.end();
    pixels = read8(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
    check(pixels.at(16 * 64 + 23) == std::array<unsigned char, 4>{0, 0, 255, 255} &&
              pixels.at(16 * 64 + 37) == std::array<unsigned char, 4>{0, 0, 255, 255} &&
              pixels.at(16 * 64 + 32)[0] > 120,
          "Layer composition ignored destination stencil/scissor or damaged its mask");

    auto hdr = target(TEX_FORMAT_RGBA16_FLOAT);
    begin(hdr, {0, 0, 2, 1});
    layer = r.PushLayer();
    r.RenderGeometry(middle, {}, 0);
    r.CompositeLayers(layer, 0, Rml::BlendMode::Blend,
                      {opacity_chain.data(), opacity_chain.size()});
    r.PopLayer();
    renderer.end();
    const auto linear = read16(hdr->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
    const auto center = linear.at(16 * 64 + 32);
    check(std::abs(float(center[0]) - .05372f) < .002f &&
              std::abs(float(center[2]) - 1.49804f) < .003f && float(center[3]) == 1,
          "HDR filtered layer was gamma-decoded twice or lost premultiplied alpha");

    for (const bool filtered : {false, true}) {
        begin(color, {0, 0, 1, 1});
        layer = r.PushLayer();
        r.RenderGeometry(rectangle, {}, 0);
        r.CompositeLayers(layer, layer, Rml::BlendMode::Replace,
                          filtered ? Rml::Span<const Rml::CompiledFilterHandle>{&opacity, 1}
                                   : Rml::Span<const Rml::CompiledFilterHandle>{});
        r.CompositeLayers(layer, 0, Rml::BlendMode::Blend, {});
        r.PopLayer();
        renderer.end();
        const auto value =
            read8(color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)).at(16 * 64 + 32);
        check(std::abs(int(value[0]) - (filtered ? 64 : 128)) <= 1 &&
                  std::abs(int(value[2]) - (filtered ? 191 : 127)) <= 1,
              "In-place Replace composition sampled its render target or lost its filter");
    }

    for (unsigned width = 65; width < 100; ++width) {
        const auto resized = target(TEX_FORMAT_RGBA8_UNORM, width, 32);
        begin(resized, {0, 0, 1, 1});
        layer = r.PushLayer();
        r.RenderGeometry(rectangle, {}, 0);
        r.CompositeLayers(layer, 0, Rml::BlendMode::Blend, {&opacity, 1});
        r.PopLayer();
        renderer.end();
        const auto statistics = renderer.filter_stats();
        check(statistics.cached_images <= 32 && statistics.cached_bytes <= 256ull * 1024 * 1024,
              "Alternating dimensions exceeded the filter scratch cache bounds");
    }

    begin(color, {0, 0, 1, 1});
    for (unsigned i = 0; i < 4; ++i)
        check(r.PushLayer() != 0, "Bounded nested filter layer failed");
    bool rejected = false;
    try {
        r.PushLayer();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && renderer.filter_stats().active_layers == 4,
          "UI layer depth admission changed the active stack");
    for (unsigned i = 0; i < 4; ++i)
        r.PopLayer();
    renderer.end();
    r.ReleaseFilter(opacity);
    r.ReleaseFilter(blur);
    r.ReleaseFilter(brightness);
    r.ReleaseFilter(contrast);
    r.ReleaseFilter(grayscale);
    std::vector<Rml::CompiledFilterHandle> bounded;
    for (unsigned i = 0; i < 256; ++i)
        bounded.push_back(r.CompileFilter("opacity", {{"value", Rml::Variant(1.f)}}));
    rejected = false;
    try {
        r.CompileFilter("opacity", {{"value", Rml::Variant(1.f)}});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && renderer.filter_stats().filters == 256,
          "Compiled filter count admission lost existing handles");
    for (const auto filter : bounded)
        r.ReleaseFilter(filter);
    r.ReleaseGeometry(rectangle);
    r.ReleaseGeometry(middle);
    r.ReleaseGeometry(mask);
    r.ReleaseGeometry(impulse);
    const auto statistics = renderer.filter_stats();
    check(statistics.active_layers == 0 && statistics.filters == 0 &&
              statistics.cached_images <= 32 && statistics.cached_bytes <= 256ull * 1024 * 1024,
          "UI filter retirement or scratch allocation bound failed");
}
} // namespace forge::test_ui_filters
