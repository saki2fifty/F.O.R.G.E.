#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "asset_bytes.hpp"
#include "fixtures/ui_png.hpp"
#include <cmath>
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_ui_luminance {
inline void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Read8, class Read16>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& root,
              const std::filesystem::path& font_path, Read8 read8, Read16 read16) {
    using namespace Diligent;
    std::filesystem::create_directories(root);
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    const auto linear = [](double c) {
        return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
    };
    constexpr std::array<std::array<unsigned char, 4>, 9> colors{{{0, 0, 0, 255},
                                                                  {255, 255, 255, 255},
                                                                  {255, 0, 0, 255},
                                                                  {0, 255, 0, 255},
                                                                  {0, 0, 255, 255},
                                                                  {128, 128, 128, 255},
                                                                  {255, 255, 255, 0},
                                                                  {255, 0, 0, 128},
                                                                  {64, 128, 192, 128}}};
    const auto hex = [](const auto& color) {
        constexpr char digits[] = "0123456789abcdef";
        std::string result = "#";
        for (const auto c : color) {
            result += digits[c >> 4];
            result += digits[c & 15];
        }
        return result;
    };
    std::array<unsigned char, 16> pixels;
    for (unsigned i = 0; i < 4; ++i)
        std::copy(colors.back().begin(), colors.back().end(), pixels.begin() + i * 4);
    const auto png = test::encode_ui_png(pixels.data(), 2, 2);
    std::ofstream bitmap(root / "mask.png", std::ios::binary);
    bitmap.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    bitmap.close();
    for (unsigned kind = 0; kind < 14; ++kind) {
        std::string mask;
        if (kind < colors.size())
            mask = "linear-gradient(90deg," + hex(colors[kind]) + "," + hex(colors[kind]) + ")";
        else if (kind == 9)
            mask = "linear-gradient(90deg,#000,#fff)";
        else if (kind == 10)
            mask = "linear-gradient(90deg,#ff000000,#ff0000ff)";
        else if (kind == 11)
            mask = "image(mask.png contain)";
        else
            mask = "linear-gradient(90deg,#808080,#808080)";
        std::ofstream source(root / "Mask.rml");
        source << "<rml><head><style>body,div{display:block;}body{margin:0;}"
                  "#panel{position:absolute;left:8px;top:8px;width:48px;height:16px;"
                  "background-color:#80402080;overflow:hidden;mask-image:"
               << (kind == 12   ? mask
                   : kind == 13 ? "named"
                                : "luminance(" + mask + ")")
               << ";}#child{position:absolute;left:16px;top:4px;width:8px;height:8px;"
                  "background-color:#00ff00;}"
               << (kind == 13 ? "@decorator named : luminance {source:" + mask + ";}" : "")
               << "</style></head><body><div id=\"panel\"><div id=\"child\"/></div></body></rml>";
        source.close();
        const auto asset = register_ui_document(root, "Mask.rml");
        for (const bool hdr : {false, true}) {
            TextureDesc desc;
            desc.Name = "FORGE luminance mask numerical oracle";
            desc.Type = RESOURCE_DIM_TEX_2D;
            desc.Width = 64;
            desc.Height = 32;
            desc.Format = hdr ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            RefCntAutoPtr<ITexture> image;
            device->CreateTexture(desc, nullptr, &image);
            require(bool(image), "Luminance oracle target unavailable");
            auto* view = image->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            UiPresenter presenter(renderer.render_interface(), root, font);
            presenter.reset("luminance-oracle", 1);
            const auto entity = EntityId::generate();
            const nlohmann::json state{
                {"version", 1},
                {"session", "luminance-oracle"},
                {"generation", 1u},
                {"revision", 1u},
                {"documents",
                 nlohmann::json::array({{{"entity", entity},
                                         {"asset", asset.id},
                                         {"instance", entity.str()},
                                         {"visible", true},
                                         {"layer", 0u},
                                         {"model", {{"tick", 0u}, {"paused", true}}},
                                         {"commands", {"Pause", "Resume", "Step"}}}})}};
            require(presenter.accept(state), presenter.diagnostic());
            presenter.update(1, 64, 32);
            context->SetRenderTargets(1, &view, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[4]{};
            context->ClearRenderTarget(view, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, view, 64, 32);
            presenter.render();
            renderer.end();
            require(presenter.diagnostic().empty(), presenter.diagnostic());
            const auto bytes = hdr ? decltype(read8(view)){} : read8(view);
            const auto halves = hdr ? read16(view) : decltype(read16(view)){};
            for (unsigned y = 0; y < 32; ++y)
                for (unsigned x = 0; x < 64; ++x) {
                    std::array<double, 4> expected{};
                    if (x >= 8 && x < 56 && y >= 8 && y < 24) {
                        const auto color = colors[kind < colors.size() ? kind : kind == 11 ? 8 : 5];
                        double coverage =
                            (linear(color[0] / 255.) * .2126 + linear(color[1] / 255.) * .7152 +
                             linear(color[2] / 255.) * .0722) *
                            color[3] / 255.;
                        const double t = (x - 8 + .5) / 48;
                        if (kind == 9)
                            coverage = linear(t);
                        if (kind == 10)
                            coverage = .2126 * t;
                        if (kind == 12)
                            coverage = 1; // Existing opaque alpha mask stays opaque gray.
                        // A contained square image has transparent horizontal margins.
                        if (kind == 11 && (x < 24 || x >= 40))
                            coverage = 0;
                        const bool child = x >= 24 && x < 32 && y >= 12 && y < 20;
                        const double alpha = child ? 1 : 128 / 255.;
                        std::array<double, 3> surface =
                            child ? std::array{0., 1., 0.}
                                  : std::array{128 / 255., 64 / 255., 32 / 255.};
                        for (unsigned c = 0; c < 3; ++c)
                            expected[c] =
                                (hdr ? linear(surface[c]) : surface[c]) * alpha * coverage;
                        expected[3] = alpha * coverage;
                    }
                    for (unsigned c = 0; c < 4; ++c) {
                        const double actual =
                            hdr ? float(halves[y * 64 + x][c]) : bytes[y * 64 + x][c] / 255.;
                        const double tolerance = hdr ? .002 : 3 / 255.;
                        if (std::abs(actual - expected[c]) > tolerance)
                            throw std::runtime_error(
                                "Luminance mask pixel differs: kind=" + std::to_string(kind) +
                                " hdr=" + std::to_string(hdr) + " x=" + std::to_string(x) +
                                " y=" + std::to_string(y) + " channel=" + std::to_string(c) +
                                " actual=" + std::to_string(actual) +
                                " expected=" + std::to_string(expected[c]));
                    }
                }
        }
    }
}
} // namespace forge::test_ui_luminance
