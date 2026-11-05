#pragma once
#include "ui_box_shadow_render_tests.hpp"
#include <RmlUi/Core.h>

namespace forge::test_ui_shadow_motion {
template <class Read8, class Read16>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& root,
              const std::filesystem::path& font_path, Read8 read8, Read16 read16) {
    using namespace Diligent;
    using test_ui_box_shadows::check;
    std::filesystem::create_directories(root);
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    const auto decode = [](double c) {
        return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4);
    };
    for (unsigned kind = 0; kind < 3; ++kind) {
        const bool inset = kind == 2, blurred = kind == 1;
        std::ofstream(root / "Motion.rml")
            << "<rml><head><style>body,div,forge-shadow-panel{display:block;}body{margin:0;}"
               "#panel,#reference{position:absolute;left:24dp;top:20dp;width:32dp;height:24dp;"
               "background-color:#00ff00;}#reference{left:152dp;}"
               "#panel{forge-shadow-y:4dp;forge-shadow-spread:2dp;forge-shadow-color:#f008;forge-"
               "shadow-inset:"
            << (inset ? "inset" : "outset")
            << ";animation:1s linear-in infinite drift;}"
               "@keyframes "
               "drift{from{forge-shadow-x:0dp;forge-shadow-color:#f008;forge-shadow-blur:0dp;}"
               "to{forge-shadow-x:16dp;forge-shadow-color:#00f8;forge-shadow-blur:"
            << (blurred ? 8 : 0)
            << "dp;}}</style></head><body><forge-shadow-panel id=\"panel\"/>"
               "<div id=\"reference\"/></body></rml>";
        const auto asset = register_ui_document(root, "Motion.rml");
        for (bool hdr : {false, true})
            for (bool output : {false, true}) {
                UiPresenter presenter(renderer.render_interface(), root, font);
                presenter.reset("shadow-motion-pixels", 1);
                const auto entity = EntityId::generate();
                const auto target_id = AssetId::generate();
                nlohmann::json document{{"entity", entity},
                                        {"asset", asset.id},
                                        {"instance", entity.str()},
                                        {"visible", true},
                                        {"layer", 0u},
                                        {"model", {{"tick", 0u}, {"paused", true}}},
                                        {"commands", {"Pause", "Resume", "Step"}}};
                if (output)
                    document["render_texture"] = target_id;
                const nlohmann::json state{{"version", 2},
                                           {"session", "shadow-motion-pixels"},
                                           {"generation", 1u},
                                           {"revision", 1u},
                                           {"documents", nlohmann::json::array({document})}};
                check(presenter.accept(state), presenter.diagnostic());
                double now = 0;
                double native_time = 0;
                for (float dp : {1.f, 1.5f, 2.f}) {
                    const unsigned width = unsigned(256 * dp), height = unsigned(80 * dp);
                    TextureDesc desc;
                    desc.Name = "FORGE shadow motion numerical oracle";
                    desc.Type = RESOURCE_DIM_TEX_2D;
                    desc.Width = width;
                    desc.Height = height;
                    desc.Format = hdr ? TEX_FORMAT_RGBA16_FLOAT : TEX_FORMAT_RGBA8_UNORM;
                    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
                    RefCntAutoPtr<ITexture> target;
                    device->CreateTexture(desc, nullptr, &target);
                    check(bool(target), "Shadow motion target unavailable");
                    auto* view = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                    for (unsigned frame = 0; frame < 4; ++frame, now += .25) {
                        const double progress = std::fmod(now, 1.);
                        const auto red = unsigned(255 * std::sqrt(1 - progress));
                        const auto blue = unsigned(255 * std::sqrt(progress));
                        const std::string color =
                            "rgba(" + std::to_string(red) + ",0," + std::to_string(blue) + ",136)";
                        Rml::Element* reference = nullptr;
                        for (int index = 0; index < Rml::GetNumContexts(); ++index) {
                            auto* native = Rml::GetContext(index);
                            if (native->GetNumDocuments()) {
                                check(!reference, "Shadow oracle found more than one document");
                                reference = native->GetDocument(0)->GetElementById("reference");
                            }
                        }
                        check(reference, "Shadow oracle reference element missing");
                        check(reference->SetProperty(
                                  "box-shadow", color + " " + std::to_string(16 * progress) +
                                                    "dp 4dp " +
                                                    std::to_string(blurred ? 8 * progress : 0) +
                                                    "dp 2dp" + (inset ? " inset" : "")),
                              "Reference shadow style rejected");
                        do {
                            native_time = std::min(now, native_time + .05);
                            presenter.update(native_time, int(width), int(height), dp);
                            if (output)
                                presenter.update_output({target_id}, int(width), int(height), dp);
                        } while (native_time + .000001 < now);
                        const std::array<float, 4> background{.125f, .25f, hdr ? 2.f : .5f, 1.f};
                        context->ClearRenderTarget(view, background.data(),
                                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        renderer.begin(context, view, int(width), int(height));
                        if (output)
                            presenter.render_output({target_id});
                        else
                            presenter.render();
                        renderer.end();
                        check(presenter.diagnostic().empty(), presenter.diagnostic());
                        const auto bytes = hdr ? decltype(read8(view)){} : read8(view);
                        const auto halves = hdr ? read16(view) : decltype(read16(view)){};
                        if (!hdr && !output && dp == 1 && frame == 0) {
                            std::ofstream image(root / ("case-" + std::to_string(kind) + ".ppm"),
                                                std::ios::binary);
                            image << "P6\n" << width << ' ' << height << "\n255\n";
                            for (const auto& p : bytes)
                                image.write(reinterpret_cast<const char*>(p.data()), 3);
                        }
                        const auto pixel = [&](unsigned x, unsigned y, unsigned c) {
                            return hdr ? double(float(halves.at(y * width + x)[c]))
                                       : bytes.at(y * width + x)[c] / 255.;
                        };
                        for (unsigned y = unsigned(8 * dp); y < unsigned(64 * dp); ++y)
                            for (unsigned x = unsigned(8 * dp); x < unsigned(96 * dp); ++x) {
                                for (unsigned c = 0; c < 4; ++c)
                                    check(std::abs(pixel(x, y, c) -
                                                   pixel(x + unsigned(128 * dp), y, c)) < .018,
                                          "Animated shadow differs from same-frame static "
                                          "reference: kind=" +
                                              std::to_string(kind) + " time=" +
                                              std::to_string(now) + " dp=" + std::to_string(dp) +
                                              " hdr=" + std::to_string(hdr) +
                                              " output=" + std::to_string(output));
                                if (blurred)
                                    continue;
                                const double px = (x + .5) / dp, py = (y + .5) / dp;
                                const bool panel = px > 24 && px < 56 && py > 20 && py < 44;
                                const double shift = 16 * progress;
                                const bool coverage =
                                    inset ? panel && !(px > 26 + shift && px < 54 + shift &&
                                                       py > 26 && py < 46)
                                          : !panel && px > 22 + shift && px < 58 + shift &&
                                                py > 22 && py < 50;
                                // Avoid native shape antialiasing boundaries, independently of the
                                // oracle.
                                if (std::min({std::abs(px - 24), std::abs(px - 56),
                                              std::abs(py - 20), std::abs(py - 44),
                                              std::abs(px - (inset ? 26 : 22) - shift),
                                              std::abs(px - (inset ? 54 : 58) - shift),
                                              std::abs(py - (inset ? 26 : 22)),
                                              std::abs(py - (inset ? 46 : 50))}) < 1)
                                    continue;
                                std::array<double, 4> expected{background[0], background[1],
                                                               background[2], 1};
                                if (panel)
                                    expected = {0, 1, 0, 1};
                                if (coverage) {
                                    const double alpha = 136 / 255.;
                                    const std::array<double, 3> color_value{
                                        hdr ? decode(red / 255.) : red / 255., 0,
                                        hdr ? decode(blue / 255.) : blue / 255.};
                                    for (unsigned c = 0; c < 3; ++c)
                                        expected[c] =
                                            color_value[c] * alpha + expected[c] * (1 - alpha);
                                }
                                for (unsigned c = 0; c < 4; ++c)
                                    check(
                                        std::abs(pixel(x, y, c) - expected[c]) < .025,
                                        "Animated sharp shadow differs from independent "
                                        "rectangle/color oracle: kind=" +
                                            std::to_string(kind) + " time=" + std::to_string(now) +
                                            " dp=" + std::to_string(dp) +
                                            " hdr=" + std::to_string(hdr) + " output=" +
                                            std::to_string(output) + " x=" + std::to_string(x) +
                                            " y=" + std::to_string(y) + " c=" + std::to_string(c) +
                                            " actual=" + std::to_string(pixel(x, y, c)) +
                                            " expected=" + std::to_string(expected[c]));
                            }
                    }
                }
            }
    }
}
} // namespace forge::test_ui_shadow_motion
