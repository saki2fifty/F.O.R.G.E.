#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <RmlUi/Core.h>
#include <array>
#include <cmath>
#include <forge/ui_diligent.hpp>
#include <stdexcept>
#include <string>

namespace forge::test_ui_snapshots {
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Read8, class Read16>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, Read8 read8, Read16 read16) {
    using namespace Diligent;
    auto& native = renderer.render_interface();
    const auto image = [&](unsigned width, unsigned height, bool linear) {
        TextureDesc desc;
        desc.Name = "FORGE cropped UI snapshot oracle";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = linear ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> texture;
        device->CreateTexture(desc, nullptr, &texture);
        check(bool(texture), "Snapshot oracle target unavailable");
        return texture;
    };
    const auto quad = [&](float x, float y, float width, float height,
                          Rml::ColourbPremultiplied tint, bool outside_uv = false) {
        const float lo = outside_uv ? -.5f : 0.f, hi = outside_uv ? 1.5f : 1.f;
        const Rml::Vertex vertices[] = {{{x, y}, tint, {lo, 0}},
                                        {{x + width, y}, tint, {hi, 0}},
                                        {{x + width, y + height}, tint, {hi, 1}},
                                        {{x, y + height}, tint, {lo, 1}}};
        const int indices[] = {0, 1, 2, 0, 2, 3};
        return native.CompileGeometry({vertices, 4}, {indices, 6});
    };
    const auto transfer = [](double value) {
        return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
    };
    for (const bool source_linear : {false, true}) {
        for (const bool destination_linear : {false, true}) {
            const auto source = image(32, 24, source_linear);
            auto* source_view = source->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            renderer.begin(context, source_view, 32, 24);
            const float clear[] = {0, 0, 0, 0};
            context->ClearRenderTarget(source_view, clear,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const auto left = quad(0, 0, 16, 8, {96, 48, 16, 128});
            const auto right = quad(16, 0, 16, 8, {24, 96, 168, 192});
            const auto bottom_left = quad(0, 8, 16, 16, {24, 96, 168, 192});
            const auto bottom_right = quad(16, 8, 16, 16, {96, 48, 16, 128});
            for (const auto geometry : {left, right, bottom_left, bottom_right})
                native.RenderGeometry(geometry, {0, 0}, 0);
            native.EnableScissorRegion(true);
            native.SetScissorRegion(Rml::Rectanglei::FromPositionSize({8, 4}, {16, 8}));
            const auto captured = native.SaveLayerAsTexture();
            check(captured != 0, "Cropped snapshot capture returned no texture");
            // Destroy the source pixels, then draw beyond the cropped viewport.
            // A retained alias or failure to restore the viewport cannot pass.
            const float changed[] = {0, .25f, 0, 1};
            context->ClearRenderTarget(source_view, changed,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            native.EnableScissorRegion(false);
            const auto marker = quad(28, 20, 4, 4, {255, 255, 255, 255});
            native.RenderGeometry(marker, {0, 0}, 0);
            for (const auto geometry : {left, right, bottom_left, bottom_right, marker})
                native.ReleaseGeometry(geometry);
            renderer.end();
            const auto source_bytes =
                source_linear ? decltype(read8(source_view)){} : read8(source_view);
            const auto source_halves =
                source_linear ? read16(source_view) : decltype(read16(source_view)){};
            for (unsigned c = 0; c < 3; ++c)
                check((source_linear ? float(source_halves.at(21 * 32 + 29)[c])
                                     : source_bytes.at(21 * 32 + 29)[c] / 255.f) > .99,
                      "Snapshot capture did not restore geometry viewport/target");
            const auto destination = image(16, 8, destination_linear);
            auto* view = destination->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            renderer.begin(context, view, 16, 8);
            const std::array<float, 4> background =
                destination_linear ? std::array{.125f, .25f, 2.f, 1.f}
                                   : std::array{32 / 255.f, 64 / 255.f, 128 / 255.f, 1.f};
            context->ClearRenderTarget(view, background.data(),
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            // This matching-sized scratch image must not reuse the pinned crop.
            const auto scratch = native.PushLayer();
            check(scratch != 0, "Snapshot pin test scratch layer unavailable");
            native.PopLayer();
            const auto display = quad(0, 0, 16, 8, {255, 255, 255, 255}, true);
            native.RenderGeometry(display, {0, 0}, captured);
            native.ReleaseGeometry(display);
            renderer.end();
            const auto bytes = destination_linear ? decltype(read8(view)){} : read8(view);
            const auto halves = destination_linear ? read16(view) : decltype(read16(view)){};
            for (unsigned y = 0; y < 8; ++y)
                for (unsigned x = 0; x < 16; ++x) {
                    const bool first = (x < 8) == (y < 4);
                    const double alpha = (first ? 128. : 192.) / 255;
                    const auto rgb =
                        first ? std::array{.75, .375, .125} : std::array{.125, .5, .875};
                    for (unsigned c = 0; c < 4; ++c) {
                        const double expected =
                            c == 3 ? 1
                                   : (destination_linear ? transfer(rgb[c]) : rgb[c]) * alpha +
                                         background[c] * (1 - alpha);
                        const double actual = destination_linear ? float(halves.at(y * 16 + x)[c])
                                                                 : bytes.at(y * 16 + x)[c] / 255.;
                        if (std::abs(actual - expected) > (destination_linear ? .004 : .012))
                            throw std::runtime_error(
                                "UI snapshot crop/pin/clamp/domain oracle differs: source_linear=" +
                                std::to_string(source_linear) + " destination_linear=" +
                                std::to_string(destination_linear) + " x=" + std::to_string(x) +
                                " y=" + std::to_string(y) + " channel=" + std::to_string(c));
                    }
                }
            native.ReleaseTexture(captured);
            const auto allocations = renderer.filter_stats().allocations;
            renderer.begin(context, view, 16, 8);
            native.PushLayer();
            native.PopLayer();
            renderer.end();
            check(renderer.filter_stats().allocations == allocations,
                  "Released snapshot did not return to the shared pool");
        }
    }
    const auto target = image(8, 8, false);
    auto* view = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    renderer.begin(context, view, 8, 8);
    native.EnableScissorRegion(true);
    native.SetScissorRegion(Rml::Rectanglei::FromPositionSize({0, 0}, {8, 8}));
    std::array<Rml::TextureHandle, 24> held{};
    for (auto& handle : held) {
        handle = native.SaveLayerAsTexture();
        check(handle != 0, "Bounded snapshot pool could not hold 24 cached images");
    }
    bool bounded = false;
    try {
        native.SaveLayerAsTexture();
    } catch (const std::runtime_error& error) {
        bounded = std::string(error.what()).find("exceed 24") != std::string::npos;
    }
    check(bounded, "Snapshot cache exceeded its 24-image bound");
    const auto allocations = renderer.filter_stats().allocations;
    native.ReleaseTexture(held[0]);
    held[0] = native.SaveLayerAsTexture();
    check(held[0] != 0 && renderer.filter_stats().allocations == allocations,
          "Released snapshot slot did not permit bounded cache recovery/reuse");
    for (auto handle : held)
        native.ReleaseTexture(handle);
    renderer.end();
}
} // namespace forge::test_ui_snapshots
