#pragma once
#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include <RmlUi/Core/ElementText.h>
#include <algorithm>
#include <forge/runtime_ui.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_ui_localization_pixels {
inline void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
inline Json read(const std::filesystem::path& path) {
    Json result;
    std::ifstream(path) >> result;
    return result;
}
inline std::string text(Rml::ElementDocument* document, const char* id) {
    auto* parent = document->GetElementById(id);
    check(parent && parent->GetNumChildren() == 1, "Localized Gallery message has unexpected DOM");
    auto* child = parent->GetChild(0);
    check(child->GetTagName() == "#text", "Localized message injected native markup");
    return static_cast<Rml::ElementText*>(child)->GetText();
}
template <class Read, class Save>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& output,
              const std::filesystem::path& font_path, const std::filesystem::path& gallery,
              Read&& readback, Save&& save) {
    const auto scratch = output / ("Localization-" + AssetId::generate().str());
    const auto root = scratch / "Gallery";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    const auto source = read(root / "Scenes/133-ui-localization.scene.json");
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    using Pixels =
        decltype(readback(device, context, static_cast<Diligent::ITextureView*>(nullptr)));
    auto present = [&](const std::filesystem::path& project, const std::string& suffix) {
        std::vector<Pixels> displays;
        for (const auto& [width, height] : {std::pair{1000u, 800u}, std::pair{420u, 720u}}) {
            Diligent::TextureDesc desc;
            desc.Name = "Localized Game UI pixel target";
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = width;
            desc.Height = height;
            desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
            Diligent::RefCntAutoPtr<Diligent::ITexture> target;
            device->CreateTexture(desc, nullptr, &target);
            check(bool(target), "Localized UI render target unavailable");
            auto* view = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            EngineContext engine(WorldRole::Runtime, false, {ui_module(project)});
            Scene scene(engine.world());
            scene.restore_snapshot(source);
            auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
            UiPresenter presenter(renderer.render_interface(), project, font);
            presenter.reset("localized-pixels", 1);
            check(presenter.accept(ui->snapshot(scene, "localized-pixels", 1, 0, true)),
                  presenter.diagnostic());
            double now = 0;
            presenter.update(++now, width, height);
            auto* native = Rml::GetContext(0);
            auto* document = native->GetDocument(0);
            auto* panel = document->GetElementById("panel");
            check(panel, "Localized Gallery panel unavailable");
            const auto origin = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
            check(origin.x >= 0 && origin.y >= 0 && origin.x + size.x <= width + .5f &&
                      origin.y + size.y <= height + .5f,
                  "Localized menu escaped its viewport");
            ui_protocol::CommandGate gate;
            gate.reset("localized-pixels", 1);
            const char* buttons[]{"english", "french", "spanish", "regional", "unsupported"};
            const char* requested[]{"en", "fr", "es", "fr-CA", "de"};
            const char* selected[]{"en", "fr", "es", "fr", "en"};
            const char* titles[]{"Language lab", "Atelier des langues", "Laboratorio de idiomas",
                                 "Atelier des langues", "Language lab"};
            for (unsigned i = 0; i < 5; ++i) {
                auto* button = document->GetElementById(buttons[i]);
                check(button, "Localized language control unavailable");
                button->ScrollIntoView(false);
                presenter.update(++now, width, height);
                const auto p = button->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto b = button->GetBox().GetSize(Rml::BoxArea::Border);
                check(p.x >= 0 && p.y >= 0 && p.x + b.x <= width && p.y + b.y <= height,
                      "Localized control is unreachable after scrolling");
                presenter.mouse_move(int(p.x + b.x / 2), int(p.y + b.y / 2), 0);
                presenter.mouse_button(0, true, 0);
                presenter.mouse_button(0, false, 0);
                check(presenter.pending_command().has_value(),
                      "Pointer did not queue locale command");
                const auto ack =
                    gate.dispatch(*presenter.pending_command(), [&](const auto& command) {
                        check(command.at("command") == "forge.locale" &&
                                  command.at("value") == requested[i],
                              "Native button produced the wrong language command");
                        ui->command(scene, command, [](const auto&) {
                            throw std::runtime_error(
                                "Language selection advanced simulation controls");
                        });
                    });
                check(ack.at("ok"), "Native language command rejected");
                presenter.acknowledge(ack);
                const auto state = ui->snapshot(scene, "localized-pixels", 1, 0, true);
                check(state.at("localization").at("locale") == requested[i],
                      "Paused native command missed the Scene locale owner");
                check(presenter.accept(state), presenter.diagnostic());
                presenter.update(++now, width, height);
                check(Rml::GetContext(0) == native && text(document, "title") == titles[i],
                      "Language update replaced context or selected the wrong messages");
                const auto observed = presenter.localization_state();
                check(observed.at("requested_locale") == requested[i] &&
                          observed.at("selected_locale") == selected[i] &&
                          observed.at("fallback_messages") == (i == 1 || i == 2 || i == 3 ? 1 : 0),
                      "Actual displayed locale/fallback observation is inaccurate");
                check(text(document, "literal").find("<b>") != std::string::npos &&
                          text(document, "literal").find("{{") != std::string::npos,
                      "Literal accented example was interpreted as markup or an expression");
                panel->SetScrollTop(0);
                presenter.mouse_move(-1, -1, 0);
                presenter.update(++now, width, height);
                context->SetRenderTargets(1, &view, nullptr,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float background[]{.02f, .03f, .04f, 1};
                context->ClearRenderTarget(view, background,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                renderer.begin(context, view, width, height);
                presenter.render();
                renderer.end();
                auto pixels = readback(device, context, view);
                const auto bright = std::count_if(pixels.begin(), pixels.end(), [](const auto& p) {
                    return p[0] > 160 && p[1] > 160 && p[2] > 160;
                });
                check(bright > 400, "Localized Game UI glyph pixels did not render");
                save(pixels, width, height,
                     output / ("ui-localization-" + std::to_string(width) + "-" + buttons[i] + "-" +
                               suffix + ".ppm"));
                if (i)
                    check(pixels != displays.back(),
                          "Changed language did not change actual pixels");
                displays.push_back(std::move(pixels));
            }
        }
        return displays;
    };
    const auto authored = present(root, "authored");
    RuntimeUiInspector inspect = [&](AssetId id, std::stop_token) {
        UiPresenter presenter(renderer.render_interface(), root, font);
        return presenter.inspect_static({id});
    };
    const std::array roots{source.at("asset_id").get<AssetId>()};
    const auto package = scratch / "Export";
    package_runtime_content(root, package, roots, {"windows", "d3d12"}, {}, {}, {}, inspect);
    const auto moved = scratch / "Relocated game";
    std::filesystem::rename(package, moved);
    std::filesystem::rename(root, scratch / "Sources offline");
    const auto exported = present(moved, "relocated");
    check(authored == exported && authored.size() == 10,
          "Source-free relocated Game UI changed its actual localized display");
    std::ofstream receipt(output / "ui-localization-pixels.json");
    receipt << Json{{"scene", roots[0]},
                    {"exact_display_pairs", 10},
                    {"locales", {"en", "fr", "es", "fr-CA", "de"}},
                    {"layouts", {{1000, 800}, {420, 720}}},
                    {"scope", "Actual native localized Game UI pixels; pointer controls during "
                              "Pause, fallback and relocated source-free export. No physical "
                              "hardware, Windows editor or graphical EXE acceptance."}}
                   .dump(2);
    check(bool(receipt.flush()), "Localized Game UI pixel receipt failed");
    std::filesystem::remove_all(scratch);
}
} // namespace forge::test_ui_localization_pixels
