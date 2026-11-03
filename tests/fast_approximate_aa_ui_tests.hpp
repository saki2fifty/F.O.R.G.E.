#pragma once
#include "display_resolve.hpp"
#include "sprite_draw_render_tests.hpp"
#include <algorithm>
#include <cstring>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_fast_aa_ui {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     Diligent::ITextureView* source, const std::filesystem::path& project,
                     const std::filesystem::path& font_path) {
    using test_sprite_draw::check;
    std::filesystem::create_directories(project);
    std::ifstream file(font_path, std::ios::binary);
    check(bool(file), "Spatial AA UI fixture font is missing");
    const std::string bytes{std::istreambuf_iterator<char>(file), {}};
    std::vector<std::byte> font(bytes.size());
    std::memcpy(font.data(), bytes.data(), bytes.size());
    UiDiligentRenderer renderer(presentation.device());
    UiPresenter presenter(renderer.render_interface(), project, font);
    presenter.reset("spatial-aa-ui", 1);
    const nlohmann::json state{{"version", 1},
                               {"session", "spatial-aa-ui"},
                               {"generation", 1u},
                               {"revision", 1u},
                               {"documents", nlohmann::json::array()}};
    check(presenter.accept(state), "Spatial AA UI state was rejected");
    DisplayResolve display(presentation);
    const auto& desc = source->GetTexture()->GetDesc();
    double time = 0;
    const auto draw = [&](bool enabled) {
        presentation.begin_frame();
        auto* image = display.resolve(context, source, 0, {}, {}, 0, false, {enabled});
        presenter.update(++time, int(desc.Width), int(desc.Height), 1);
        renderer.begin(context, display.target(), desc.Width, desc.Height);
        presenter.render();
        renderer.end();
        presentation.end_frame(context);
        return test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
    };
    const auto background = draw(false);
    presenter.frame_statistics("FXAA HUD | 12345");
    const auto ordinary_ui = draw(false);
    const auto filtered_ui = draw(true);
    check(ordinary_ui == filtered_ui && draw(false) == ordinary_ui,
          "Enabling or disabling spatial AA changed subsequent native Game UI pixels");
    unsigned changed = 0, bright = 0;
    for (std::size_t i = 0; i < background.size(); ++i) {
        changed += background[i] != ordinary_ui[i];
        bright += ordinary_ui[i][0] > 220 && ordinary_ui[i][1] > 220 && ordinary_ui[i][2] > 220;
    }
    check(changed > 100 && bright > 20, "Spatial AA UI comparison contained no rendered text");
    context->WaitForIdle();
}
} // namespace forge::test_fast_aa_ui
