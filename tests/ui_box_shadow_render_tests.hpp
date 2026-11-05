#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "asset_bytes.hpp"
#include <cmath>
#include <forge/ui_assets.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_ui_box_shadows {
inline void check(bool value, const std::string& message) {
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
    for (unsigned kind = 0; kind < 7; ++kind) {
        const int spread = kind == 1 || kind == 3 ? 4 : kind == 2 ? -4 : 0;
        const int radius = kind == 4 ? 8 : 0;
        const int blur = kind == 5 ? 8 : 0;
        std::ofstream(root / "Shadow.rml")
            << "<rml><head><style>body,div{display:block;}body{margin:0;font-family:Lato;}"
               "#panel{position:absolute;left:24px;top:20px;width:32px;height:24px;"
               "background-color:#00ff00;box-shadow:#ff000080 8px 6px "
            << blur << "px " << spread << "px" << (kind == 3 ? " inset" : "")
            << ";border-radius:" << radius << "px;opacity:" << (kind == 6 ? "0.5" : "1")
            << ";}"
               "#marker{position:absolute;left:80px;top:64px;width:4px;height:4px;"
               "background-color:#0000ff;}</style></head><body><div id=\"panel\"/>"
               "<div id=\"marker\"/></body></rml>";
        const auto asset = register_ui_document(root, "Shadow.rml");
        for (bool linear : {false, true}) {
            TextureDesc desc;
            desc.Name = "FORGE native box shadow oracle";
            desc.Type = RESOURCE_DIM_TEX_2D;
            desc.Width = 96;
            desc.Height = 80;
            desc.Format = linear ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            RefCntAutoPtr<ITexture> target;
            device->CreateTexture(desc, nullptr, &target);
            check(bool(target), "Shadow target unavailable");
            auto* view = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            UiPresenter presenter(renderer.render_interface(), root, font);
            presenter.reset("box-shadow-oracle", 1);
            const auto id = EntityId::generate();
            nlohmann::json state{{"version", 1},
                                 {"session", "box-shadow-oracle"},
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
            check(presenter.accept(state), presenter.diagnostic());
            const std::array<float, 4> background =
                linear ? std::array{.125f, .25f, 2.f, 1.f}
                       : std::array{32 / 255.f, 64 / 255.f, 128 / 255.f, 1.f};
            // First actual render must populate the cache left empty by preparation.
            for (unsigned frame = 0; frame < 2; ++frame) {
                presenter.update(frame + 1, 96, 80);
                context->ClearRenderTarget(view, background.data(),
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                renderer.begin(context, view, 96, 80);
                presenter.render();
                renderer.end();
                check(presenter.diagnostic().empty(), presenter.diagnostic());
                const auto bytes = linear ? decltype(read8(view)){} : read8(view);
                const auto halves = linear ? read16(view) : decltype(read16(view)){};
                const auto rounded = [](double x, double y, double l, double t, double w, double h,
                                        double rad) {
                    const double dx = std::max(std::max(l + rad - x, x - (l + w - rad)), 0.);
                    const double dy = std::max(std::max(t + rad - y, y - (t + h - rad)), 0.);
                    if (!rad)
                        return x > l && x < l + w && y > t && y < t + h;
                    return dx * dx + dy * dy < rad * rad;
                };
                const auto mass = [](double coordinate, double low, double high) {
                    double total = 0, covered = 0;
                    for (int d = -12; d <= 12; ++d) {
                        const double weight = std::exp(-.5 * (d / 4.) * (d / 4.));
                        total += weight;
                        if (coordinate + d >= low && coordinate + d < high)
                            covered += weight;
                    }
                    return covered / total;
                };
                for (int y = 0; y < 80; ++y)
                    for (int x = 0; x < 96; ++x) {
                        const double px = x + .5, py = y + .5;
                        // Rounded boundaries carry native geometry antialiasing.
                        // Check stable interiors/exteriors, not its tessellation convention.
                        if (radius) {
                            const auto near_edge = [&](double left, double top) {
                                const double dx =
                                    std::max({left + radius - px, px - (left + 32 - radius), 0.});
                                const double dy =
                                    std::max({top + radius - py, py - (top + 24 - radius), 0.});
                                return std::abs(std::hypot(dx, dy) - radius) < 1.25;
                            };
                            if (near_edge(24, 20) || near_edge(32, 26))
                                continue;
                        }
                        const bool panel = rounded(px, py, 24, 20, 32, 24, radius);
                        double coverage = kind == 5
                                              ? (mass(px, 32, 64) * mass(py, 26, 50) -
                                                 mass(px, 32, 56) * mass(py, 26, 44))
                                              : rounded(px, py, 32 - spread, 26 - spread,
                                                        32 + 2 * spread, 24 + 2 * spread, radius);
                        if (kind == 3)
                            coverage = panel && !(px >= 36 && px < 60 && py >= 30 && py < 46);
                        const double opacity = kind == 6 ? .5 : 1.;
                        const double alpha = 128 / 255. * coverage;
                        std::array<double, 4> expected{background[0], background[1], background[2],
                                                       1};
                        if (panel) {
                            expected = {0, opacity, 0, 1};
                            for (unsigned c = 0; c < 3; ++c)
                                expected[c] += background[c] * (1 - opacity);
                            if (kind == 3)
                                for (unsigned c = 0; c < 3; ++c)
                                    expected[c] = (c == 0 ? alpha : 0) + expected[c] * (1 - alpha);
                        } else if (coverage > 0)
                            for (unsigned c = 0; c < 3; ++c)
                                expected[c] = (c == 0 ? alpha * opacity : 0) +
                                              background[c] * (1 - alpha * opacity);
                        if (x >= 80 && x < 84 && y >= 64 && y < 68)
                            expected = {0, 0, 1, 1};
                        for (unsigned c = 0; c < 4; ++c) {
                            const double actual = linear ? float(halves.at(y * 96 + x)[c])
                                                         : bytes.at(y * 96 + x)[c] / 255.;
                            const double tolerance = kind == 5 ? .025 : .014;
                            check(std::abs(actual - expected[c]) < tolerance,
                                  "Native box shadow differs: kind=" + std::to_string(kind) +
                                      " linear=" + std::to_string(linear) + " frame=" +
                                      std::to_string(frame) + " x=" + std::to_string(x) +
                                      " y=" + std::to_string(y) + " c=" + std::to_string(c) +
                                      " expected=" + std::to_string(expected[c]) +
                                      " actual=" + std::to_string(actual));
                        }
                    }
            }
        }
    }
}
} // namespace forge::test_ui_box_shadows
