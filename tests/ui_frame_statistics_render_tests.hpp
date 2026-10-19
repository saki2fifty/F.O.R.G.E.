#pragma once
#include <RmlUi/Core.h>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>
namespace forge::test_ui_frame_statistics {
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Read, class Save>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& output,
              const std::filesystem::path& font_path, Read&& readback, Save&& save) {
    std::ifstream stream(font_path, std::ios::binary);
    check(bool(stream), "Frame statistics font missing");
    const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
    std::vector<std::byte> font(bytes.size());
    std::memcpy(font.data(), bytes.data(), bytes.size());
    struct Layout {
        unsigned width, height;
        float density;
    };
    const Layout layouts[] = {{800, 600, 1}, {1280, 720, 1.5f}, {960, 640, 2}, {320, 200, 1},
                              {320, 240, 2}, {720, 1280, 2},    {320, 200, 4}};
    nlohmann::json records = nlohmann::json::array();
    const std::string label = "FPS 60.0 | Frame 16.67 ms | Peak 19.42 ms";
    unsigned index = 0;
    for (const auto layout : layouts) {
        Diligent::TextureDesc desc;
        desc.Name = "FORGE FPS overlay pixel target";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = layout.width;
        desc.Height = layout.height;
        desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
        Diligent::RefCntAutoPtr<Diligent::ITexture> target;
        device->CreateTexture(desc, nullptr, &target);
        check(bool(target), "Frame statistics render target allocation failed");
        auto* view = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        UiPresenter presenter(renderer.render_interface(), output, font);
        presenter.reset("frame-pixels", 1);
        nlohmann::json state{{"version", 1},
                             {"session", "frame-pixels"},
                             {"generation", 1u},
                             {"revision", 1u},
                             {"documents", nlohmann::json::array()}};
        check(presenter.accept(state), presenter.diagnostic().c_str());
        double time = 0;
        const auto render = [&] {
            context->SetRenderTargets(1, &view, nullptr,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float blue[] = {0, 0, 1, 1};
            context->ClearRenderTarget(view, blue,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            presenter.update(++time, layout.width, layout.height, layout.density);
            renderer.begin(context, view, layout.width, layout.height);
            presenter.render();
            renderer.end();
            return readback(device, context, view);
        };
        const auto baseline = render();
        check(std::all_of(baseline.begin(), baseline.end(),
                          [](const auto& pixel) {
                              return pixel == std::array<unsigned char, 4>{0, 0, 255, 255};
                          }),
              "Disabled FPS overlay changed the base frame");
        presenter.frame_statistics(label);
        const auto pixels = render();
        auto* panel = Rml::GetContext(0)->GetDocument(0)->GetElementById("forge-frame-statistics");
        check(panel, "Native FPS panel is missing");
        const auto origin = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
        save(pixels, layout.width, layout.height,
             output / ("frame-statistics-" + std::to_string(index) + ".ppm"));
        check(origin.x >= 0 && origin.y >= 0 && origin.x + size.x <= layout.width + .5f &&
                  origin.y + size.y <= layout.height + .5f && size.x > 0 && size.y > 0,
              "Native FPS panel is outside the viewport");
        unsigned changed = 0, bright = 0;
        for (unsigned y = 0; y < layout.height; ++y)
            for (unsigned x = 0; x < layout.width; ++x) {
                const auto& pixel = pixels[y * layout.width + x];
                if (pixel != baseline[y * layout.width + x]) {
                    ++changed;
                    if (!(x + 1 >= origin.x && y + 1 >= origin.y && x <= origin.x + size.x + 1 &&
                          y <= origin.y + size.y + 1))
                        throw std::runtime_error(
                            "FPS pixel outside panel: " + std::to_string(x) + "," +
                            std::to_string(y) + " panel " + std::to_string(origin.x) + "," +
                            std::to_string(origin.y) + " " + std::to_string(size.x) + "x" +
                            std::to_string(size.y));
                }
                bright += pixel[0] > 230 && pixel[1] > 230 && pixel[2] > 230;
            }
        check(changed > 100 && bright > 25, "Actual FPS background or text glyphs did not render");
        check(render() == pixels, "FPS drawing accumulated or changed stable pixels");
        ++index;
        presenter.frame_statistics("FPS 30.0 | Frame 33.33 ms | Peak 40.00 ms");
        check(render() != pixels, "Changed FPS reading did not reach native pixels");
        presenter.frame_statistics("");
        check(render() == baseline, "Disabling FPS failed to restore exact game pixels");
        records.push_back({{"width", layout.width},
                           {"height", layout.height},
                           {"density", layout.density},
                           {"changed_pixels", changed},
                           {"bright_pixels", bright},
                           {"authored_documents", presenter.document_count()}});
    }
    std::ofstream receipt(output / "frame-statistics-pixels.json");
    receipt << records.dump(2);
    check(bool(receipt.flush()), "Frame statistics pixel receipt failed");
}
} // namespace forge::test_ui_frame_statistics
