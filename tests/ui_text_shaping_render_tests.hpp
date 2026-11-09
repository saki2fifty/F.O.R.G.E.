#pragma once
#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_diligent.hpp>
#include <fstream>

namespace forge::test_ui_shaping_pixels {
inline void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
inline Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
template <class Read, class Save>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& output,
              const std::filesystem::path& font_path, const std::filesystem::path& example,
              Read&& readback, Save&& save) {
    const auto scratch = output / ("TextShaping-" + AssetId::generate().str());
    const auto root = scratch / "source";
    std::filesystem::create_directories(scratch);
    std::filesystem::copy(example, root, std::filesystem::copy_options::recursive);
    const auto source = read(root / "Scenes/TextShaping.scene.json");
    const auto settings = ProjectSettings(root).document();
    check(ui_text_shaping_enabled(settings.at("game")), "Example does not enable UI text shaping");
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    using Pixels =
        decltype(readback(device, context, static_cast<Diligent::ITextureView*>(nullptr)));
    auto present = [&](const std::filesystem::path& project, bool shaping,
                       const std::string& suffix) {
        std::vector<Pixels> displays;
        for (unsigned width : {1000u, 420u}) {
            constexpr unsigned height = 800;
            Diligent::TextureDesc desc;
            desc.Name = "Native UI text shaping pixel target";
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = width;
            desc.Height = height;
            desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
            Diligent::RefCntAutoPtr<Diligent::ITexture> target;
            device->CreateTexture(desc, nullptr, &target);
            check(bool(target), "UI text shaping target allocation failed");
            auto* view = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            EngineContext engine(WorldRole::Runtime, false, {ui_module(project)});
            Scene scene(engine.world());
            scene.restore_snapshot(source);
            auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
            UiPresenter presenter(renderer.render_interface(), project, font, {}, nullptr, shaping,
                                  ui_grapheme_editing_enabled(settings.at("game")));
            presenter.reset("shape-pixels", 1);
            const bool accepted =
                presenter.accept(ui->snapshot(scene, "shape-pixels", 1, 41, true));
            check(accepted, presenter.diagnostic());
            presenter.update(0, width, height);
            auto* document = Rml::GetContext(0)->GetDocument(0);
            auto* panel = document->GetElementById("panel");
            check(panel, "Shaped example panel is missing");
            const auto position = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
            check(position.x >= 0 && position.y >= 0 && position.x + size.x <= width + .5 &&
                      position.y + size.y <= height + .5,
                  "Text shaping panel escaped the viewport");
            context->SetRenderTargets(1, &view, nullptr,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[]{.02f, .04f, .07f, 1};
            context->ClearRenderTarget(view, clear,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, view, width, height);
            presenter.render();
            renderer.end();
            check(presenter.diagnostic().empty(), presenter.diagnostic());
            auto pixels = readback(device, context, view);
            save(pixels, width, height,
                 output / ("ui-text-" + suffix + "-" + std::to_string(width) + ".ppm"));
            const auto bright = std::ranges::count_if(pixels, [](const auto& pixel) {
                return pixel[0] > 150 && pixel[1] > 150 && pixel[2] > 150;
            });
            check(bright > 500, "Native font glyphs did not reach the rendered image");
            displays.push_back(std::move(pixels));
            auto* weights = document->GetElementById("weights");
            check(weights, "Variable weight comparison missing from native sample");
            weights->ScrollIntoView(false);
            presenter.update(.02, width, height);
            context->ClearRenderTarget(view, clear,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, view, width, height);
            presenter.render();
            renderer.end();
            check(presenter.diagnostic().empty(), presenter.diagnostic());
            auto weight_pixels = readback(device, context, view);
            save(weight_pixels, width, height,
                 output / ("ui-text-weights-" + suffix + "-" + std::to_string(width) + ".ppm"));
            if (shaping) {
                std::array<std::size_t, 3> coverage{};
                unsigned sample = 0;
                for (const char* id : {"weight-light", "weight-regular", "weight-heavy"}) {
                    auto* glyph = document->GetElementById(id);
                    check(glyph, "Native variable glyph element missing");
                    const auto origin = glyph->GetAbsoluteOffset(Rml::BoxArea::Content);
                    const auto dimensions = glyph->GetBox().GetSize(Rml::BoxArea::Content);
                    const int left = std::max(0, int(std::floor(origin.x))),
                              right = std::min(int(width), int(std::ceil(origin.x + dimensions.x))),
                              top = std::max(0, int(std::floor(origin.y))),
                              bottom =
                                  std::min(int(height), int(std::ceil(origin.y + dimensions.y)));
                    for (int y = top; y < bottom; ++y)
                        for (int x = left; x < right; ++x) {
                            const auto& pixel = weight_pixels[std::size_t(y) * width + unsigned(x)];
                            coverage[sample] += pixel[0] > 150 && pixel[1] > 150 && pixel[2] > 150;
                        }
                    ++sample;
                }
                check(coverage[0] > 50 && coverage[0] < coverage[1] && coverage[1] < coverage[2],
                      "Native variable weight pixel coverage is not light < regular < heavy");
            }
            displays.push_back(std::move(weight_pixels));
        }
        return displays;
    };
    const auto baseline = present(root, false, "baseline");
    const auto shaped = present(root, true, "shaped");
    for (std::size_t i = 0; i < baseline.size(); ++i) {
        std::size_t changed = 0;
        for (std::size_t p = 0; p < baseline[i].size(); ++p)
            changed += baseline[i][p] != shaped[i][p];
        check(changed > 100, "Shaped native text did not differ from the legacy glyph path");
    }
    const std::array roots{source.at("asset_id").get<AssetId>()};
    RuntimeUiInspector inspector = [&](AssetId id, std::stop_token) {
        UiPresenter presenter(renderer.render_interface(), root, font, {}, nullptr, true,
                              ui_grapheme_editing_enabled(settings.at("game")));
        return presenter.inspect_static({id});
    };
    package_runtime_content(root, scratch / "export", roots, {"windows", "d3d12"}, {}, {}, {},
                            inspector);
    const auto moved = scratch / "relocated";
    std::filesystem::rename(scratch / "export", moved);
    (void)open_runtime_content(moved, {"windows", "d3d12"});
    std::filesystem::rename(root, scratch / "source-offline");
    const auto relocated =
        present(moved, ui_text_shaping_enabled(settings.at("game")), "relocated");
    check(shaped == relocated, "Relocated shaped text pixels differ with source unavailable");
    context->WaitForIdle();
}
} // namespace forge::test_ui_shaping_pixels
