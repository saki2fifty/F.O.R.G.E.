#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "asset_bytes.hpp"
#include <RmlUi/Core.h>
#include <cmath>
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_ui_backdrop {
inline void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class Read8, class Read16>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& root,
              const std::filesystem::path& font_path, Read8 read8, Read16 read16) {
    using namespace Diligent;
    std::filesystem::create_directories(root);
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    for (const bool foreground_filter : {false, true}) {
        std::string source = R"rml(<rml><head><style>
body,div {display:block;} body {margin:0;font-family:Lato;}
#left,#right {position:absolute;top:0;width:32px;height:32px;}
#left {left:0;background-color:#800000;} #right {left:32px;background-color:#0000ff;}
#glass {position:absolute;left:16px;top:4px;width:32px;height:24px;border-radius:6px;}
#front {position:absolute;left:12px;top:8px;width:8px;height:8px;background-color:#00ff00;}
</style></head><body><div id="left"/><div id="right"/>
<div id="glass" data-style-backdrop-filter="'blur(' + blur + 'px) brightness(0.5)'" STYLE><div id="front"/></div>
</body></rml>)rml";
        source.replace(source.find("STYLE"), 5,
                       foreground_filter ? "style=\"filter:opacity(0.5)\"" : "");
        std::ofstream(root / "Backdrop.rml") << source;
        const auto asset = register_ui_document(root, "Backdrop.rml");
        for (const bool linear : {false, true}) {
            TextureDesc desc;
            desc.Name = "FORGE native backdrop oracle";
            desc.Type = RESOURCE_DIM_TEX_2D;
            desc.Width = 64;
            desc.Height = 32;
            desc.Format = linear ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            RefCntAutoPtr<ITexture> image;
            device->CreateTexture(desc, nullptr, &image);
            check(bool(image), "Backdrop oracle target unavailable");
            auto* view = image->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            UiPresenter presenter(renderer.render_interface(), root, font);
            presenter.reset("backdrop-oracle", 1);
            const auto id = EntityId::generate();
            nlohmann::json state{
                {"version", 1},
                {"session", "backdrop-oracle"},
                {"generation", 1u},
                {"revision", 1u},
                {"documents",
                 nlohmann::json::array({{{"entity", id},
                                         {"asset", asset.id},
                                         {"instance", id.str()},
                                         {"visible", true},
                                         {"layer", 0u},
                                         {"model", {{"tick", 0u}, {"paused", true}, {"blur", 2.}}},
                                         {"commands", {"Pause", "Resume", "Step"}}}})}};
            const bool accepted = presenter.accept(state);
            check(accepted, presenter.diagnostic().c_str());
            auto* native = Rml::GetContext(0);
            for (const unsigned sigma : {2u, 0u}) {
                state["documents"][0]["model"]["blur"] = double(sigma);
                state["revision"] = state.at("revision").get<unsigned>() + 1;
                const bool updated = presenter.accept(state);
                check(updated, presenter.diagnostic().c_str());
                presenter.update(1, 64, 32);
                context->SetRenderTargets(1, &view, nullptr,
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float clear[]{0, 0, 0, 1};
                context->ClearRenderTarget(view, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                renderer.begin(context, view, 64, 32);
                presenter.render();
                renderer.end();
                check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
                check(Rml::GetContext(0) == native && renderer.filter_stats().active_layers == 0,
                      "Backdrop model update replaced its context or retained a layer");
                const auto bytes =
                    linear ? std::vector<std::array<unsigned char, 4>>{} : read8(view);
                const auto halves = linear ? read16(view) : decltype(read16(view)){};
                const auto pixel = [&](int x, int y, unsigned c) -> double {
                    const auto at = std::size_t(y * 64 + x);
                    return linear ? float(halves.at(at)[c]) : bytes.at(at)[c] / 255.;
                };
                const double red = linear ? .2158605001 : 128. / 255;
                const int radius = int(sigma * 3);
                double normalization = 0;
                for (int dx = -radius; dx <= radius; ++dx)
                    normalization += sigma ? std::exp(-.5 * dx * dx / (sigma * sigma)) : 1;
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 64; ++x) {
                        const bool outside = x < 16 || x >= 48 || y < 4 || y >= 28;
                        const bool interior = x >= 22 && x < 42 && y >= 10 && y < 22;
                        if (!outside && !interior)
                            continue; // Rounded/edge raster coverage has a separate corner check.
                        const std::array<double, 4> background{x < 32 ? red : 0, 0,
                                                               x >= 32 ? 1. : 0, 1};
                        auto expected = background;
                        if (interior) {
                            double left = 0;
                            for (int dx = -radius; dx <= radius; ++dx)
                                if (x + dx < 32)
                                    left += sigma ? std::exp(-.5 * dx * dx / (sigma * sigma)) : 1;
                            left /= normalization;
                            expected = {red * left * .5, 0, (1 - left) * .5, 1};
                            if (x >= 28 && x < 36 && y >= 12 && y < 20)
                                expected = {0, 1, 0, 1};
                            if (foreground_filter)
                                for (unsigned c = 0; c < 3; ++c)
                                    expected[c] = .5 * (expected[c] + background[c]);
                        }
                        for (unsigned c = 0; c < 4; ++c)
                            check(std::abs(pixel(x, y, c) - expected[c]) < (linear ? .003 : .015),
                                  "Native backdrop altered foreground/outside pixels or lost "
                                  "blur/domain/source-over semantics");
                    }
                check(std::abs(pixel(16, 4, 0) - red) < .006 && pixel(16, 4, 2) < .006,
                      "Backdrop escaped its rounded border clipping");
            }
        }
    }
}
} // namespace forge::test_ui_backdrop
