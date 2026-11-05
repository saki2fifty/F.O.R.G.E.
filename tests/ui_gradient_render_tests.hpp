#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "asset_bytes.hpp"
#include "fixtures/ui_gradient.hpp"
#include <RmlUi/Core.h>
#include <cmath>
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_ui_gradients {
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
    for (unsigned kind = 0; kind < 8; ++kind) {
        std::string declaration;
        switch (kind) {
        case 0:
            declaration = "linear-gradient(90deg,#ff0000,#0000ff)";
            break;
        case 1:
            declaration = "linear-gradient(180deg,#00ff00,#0000ff)";
            break;
        case 2:
            declaration = "repeating-linear-gradient(90deg,#ff000080 25%,#0000ff 75%)";
            break;
        case 3:
            declaration = "horizontal-gradient(#ff0000 #0000ff)";
            break;
        case 4:
            declaration = "vertical-gradient(#00ff00 #0000ff)";
            break;
        case 5:
            declaration = "linear-gradient(45deg,#ff0000 0%,#00ff00 50%,#0000ff 100%)";
            break;
        case 6:
            declaration = "linear-gradient(90deg";
            for (unsigned i = 0; i < 16; ++i)
                declaration += std::string(i % 2 ? ",#0000ff " : ",#ff0000 ") +
                               std::to_string(i * 100. / 15) + "%";
            declaration += ')';
            break;
        default:
            declaration = "linear-gradient(90deg,#ff0000 50%,#0000ff 50%)";
            break;
        }
        std::ofstream(root / "Gradient.rml")
            << "<rml><head><style>body,div {display:block;} body {margin:0;font-family:Lato;}"
               "#gradient "
               "{position:absolute;left:8px;top:4px;width:48px;height:24px;border-radius:4px;"
               "decorator:"
            << declaration
            << ";} #marker "
               "{position:absolute;left:30px;top:14px;width:4px;height:4px;background-color:#"
               "00ff00;}"
               "</style></head><body><div id=\"gradient\"/><div id=\"marker\"/></body></rml>";
        const auto asset = register_ui_document(root, "Gradient.rml");
        for (const bool linear : {false, true}) {
            TextureDesc desc;
            desc.Name = "FORGE native linear gradient oracle";
            desc.Type = RESOURCE_DIM_TEX_2D;
            desc.Width = 64;
            desc.Height = 32;
            desc.Format = linear ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            RefCntAutoPtr<ITexture> image;
            device->CreateTexture(desc, nullptr, &image);
            check(bool(image), "Gradient oracle target unavailable");
            auto* view = image->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            UiPresenter presenter(renderer.render_interface(), root, font);
            presenter.reset("gradient-oracle", 1);
            const auto id = EntityId::generate();
            nlohmann::json state{{"version", 1},
                                 {"session", "gradient-oracle"},
                                 {"generation", 1u},
                                 {"revision", 1u},
                                 {"documents", nlohmann::json::array(
                                                   {{{"entity", id},
                                                     {"asset", asset.id},
                                                     {"instance", id.str()},
                                                     {"visible", true},
                                                     {"layer", 0u},
                                                     {"model", {{"tick", 0u}, {"paused", true}}},
                                                     {"commands", {"Pause", "Resume", "Step"}}}})}};
            const bool accepted = presenter.accept(state);
            check(accepted, presenter.diagnostic().c_str());
            presenter.update(1, 64, 32);
            const std::array<float, 4> background =
                linear ? std::array{.125f, .25f, 2.f, 1.f}
                       : std::array{32 / 255.f, 64 / 255.f, 128 / 255.f, 1.f};
            context->SetRenderTargets(1, &view, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearRenderTarget(view, background.data(),
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, view, 64, 32);
            presenter.render();
            renderer.end();
            check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
            const auto bytes = linear ? std::vector<std::array<unsigned char, 4>>{} : read8(view);
            const auto halves = linear ? read16(view) : decltype(read16(view)){};
            const auto transfer = [](double c) {
                return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
            };
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 64; ++x) {
                    const bool outside = x < 8 || x >= 56 || y < 4 || y >= 28;
                    const bool interior = x >= 12 && x < 52 && y >= 8 && y < 24;
                    if (!outside && !interior)
                        continue;
                    std::array<double, 4> expected{background[0], background[1], background[2], 1};
                    if (interior) {
                        double t = (kind == 1 || kind == 4) ? (y - 4 + .5) / 24 : (x - 8 + .5) / 48;
                        if (kind == 2)
                            t = (t - .25) / .5 - std::floor((t - .25) / .5);
                        if (kind == 5)
                            t = ((x - 8 + .5) - (y - 4 + .5) + 24) / 72;
                        const double first_alpha = kind == 2 ? 128. / 255 : 1;
                        const double alpha = first_alpha * (1 - t) + t;
                        std::array<double, 3> rgb{(kind == 1 || kind == 4) ? 0
                                                                           : first_alpha * (1 - t),
                                                  (kind == 1 || kind == 4) ? 1 - t : 0, t};
                        if (kind == 5)
                            rgb = t < .5 ? std::array{1 - 2 * t, 2 * t, 0.}
                                         : std::array{0., 2 - 2 * t, 2 * t - 1};
                        if (kind == 6) {
                            const double segment = std::floor(t * 15), blend = t * 15 - segment;
                            rgb = int(segment) % 2 ? std::array{blend, 0., 1 - blend}
                                                   : std::array{1 - blend, 0., blend};
                        }
                        if (kind == 7)
                            rgb = t < .5 ? std::array{1., 0., 0.} : std::array{0., 0., 1.};
                        for (unsigned c = 0; c < 3; ++c) {
                            if (linear)
                                rgb[c] = transfer(rgb[c] / alpha) * alpha;
                            expected[c] = rgb[c] + background[c] * (1 - alpha);
                        }
                        if (x >= 30 && x < 34 && y >= 14 && y < 18)
                            expected = {0, 1, 0, 1};
                    }
                    const auto at = std::size_t(y * 64 + x);
                    for (unsigned c = 0; c < 4; ++c) {
                        const double actual =
                            linear ? float(halves.at(at)[c]) : bytes.at(at)[c] / 255.;
                        check(std::abs(actual - expected[c]) < (linear ? .004 : .01),
                              "Native linear gradient projection/repeat/premultiplied color/domain "
                              "or ordinary draw restoration differs");
                    }
                }
        }
    }
    auto& native = renderer.render_interface();
    std::vector<Rml::CompiledShaderHandle> handles;
    for (unsigned i = 0; i < 256; ++i)
        handles.push_back(
            native.CompileShader("linear-gradient", test::linear_gradient_parameters()));
    bool rejected = false;
    try {
        native.CompileShader("linear-gradient", test::linear_gradient_parameters());
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected,
          "Compiled gradient count exceeded its bound or native contexts retained shader rows");
    for (const auto handle : handles)
        native.ReleaseShader(handle);
}
} // namespace forge::test_ui_gradients
