#pragma once
#include "application_state_gallery_fixture.hpp"
#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>

namespace forge::test_state_gallery_pixels {
template <class Read, class Save>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& output,
              const std::filesystem::path& font_path, const std::filesystem::path& gallery,
              Read&& readback, Save&& save, bool activity = false) {
    using namespace test_state_gallery;
    const auto scratch = output / ("States-" + AssetId::generate().str());
    const auto root = scratch / "Gallery";
    std::filesystem::create_directories(root);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    const auto selected_scene = activity ? "Scenes/135-state-activity.scene.json" : scene_path;
    const auto source = read(root / selected_scene);
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    using Pixels =
        decltype(readback(device, context, static_cast<Diligent::ITextureView*>(nullptr)));
    const auto present = [&](const std::filesystem::path& project, const Json& scene,
                             const std::string& suffix) {
        std::vector<Pixels> frames;
        for (const auto& [width, height] : {std::pair{1000u, 900u}, std::pair{420u, 720u}}) {
            Diligent::TextureDesc desc;
            desc.Name = activity ? "Activity Lab native controls" : "Signal Lock native game UI";
            desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
            desc.Width = width;
            desc.Height = height;
            desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
            desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
            Diligent::RefCntAutoPtr<Diligent::ITexture> target;
            device->CreateTexture(desc, nullptr, &target);
            check(bool(target), "Signal Lock native render target unavailable");
            auto view = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
            Game game(project, scene, activity);
            const auto original = game.scene().snapshot();
            UiPresenter presenter(renderer.render_interface(), project, font);
            auto snapshot = game.snapshot();
            presenter.reset(snapshot.at("session"), snapshot.at("generation"));
            check(presenter.accept(snapshot), presenter.diagnostic());
            ui_protocol::CommandGate gate;
            gate.reset(snapshot.at("session"), snapshot.at("generation"));
            double now = 0;
            presenter.update(++now, width, height);
            auto native = Rml::GetContext(0);
            auto document = native->GetDocument(0);
            const auto panel = document->GetElementById("panel");
            const auto origin = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
            check(origin.x >= 0 && origin.y >= 0 && origin.x + size.x <= width + .5f &&
                      origin.y + size.y <= height + .5f,
                  "Signal Lock panel escaped viewport");
            const auto capture = [&](const char* name) {
                context->SetRenderTargets(1, &view, nullptr,
                                          Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float background[]{.02f, .03f, .04f, 1};
                context->ClearRenderTarget(view, background,
                                           Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                renderer.begin(context, view, width, height);
                presenter.render();
                renderer.end();
                auto pixels = readback(device, context, view);
                save(pixels, width, height,
                     output / ((activity ? "ui-state-conditions-" : "ui-states-") +
                               std::to_string(width) + "-" + name + "-" + suffix + ".ppm"));
                check(std::count_if(pixels.begin(), pixels.end(),
                                    [](const auto& pixel) {
                                        return pixel[0] > 160 && pixel[1] > 160 && pixel[2] > 160;
                                    }) > 400,
                      "Application state UI produced no actual glyph pixels");
                if (!frames.empty())
                    check(pixels != frames.back(),
                          "Signal Lock transition did not change visible pixels");
                frames.push_back(std::move(pixels));
            };
            capture("menu");
            const std::vector<const char*> buttons =
                activity
                    ? std::vector<const char*>{"run", "rest", "hide", "step"}
                    : std::vector<const char*>{"play", "right", "cyan", "finish", "menu-button"};
            const std::vector<const char*> names =
                activity
                    ? std::vector<const char*>{"playing", "rest", "hidden", "step"}
                    : std::vector<const char*>{"playing", "wrong", "open", "complete", "reset"};
            const std::vector<const char*> modes =
                activity
                    ? std::vector<const char*>{"playing", "rest", "menu", "menu"}
                    : std::vector<const char*>{"playing", "playing", "playing", "complete", "menu"};
            for (unsigned index = 0; index < buttons.size(); ++index) {
                auto button = document->GetElementById(buttons[index]);
                check(button, "Signal Lock pointer target missing");
                button->ScrollIntoView(false);
                presenter.update(++now, width, height);
                const auto position = button->GetAbsoluteOffset(Rml::BoxArea::Border);
                const auto extent = button->GetBox().GetSize(Rml::BoxArea::Border);
                check(position.x >= 0 && position.y >= 0 && position.x + extent.x <= width &&
                          position.y + extent.y <= height,
                      "Signal Lock control is unreachable after scrolling");
                presenter.mouse_move(int(position.x + extent.x / 2), int(position.y + extent.y / 2),
                                     0);
                presenter.mouse_button(0, true, 0);
                presenter.mouse_button(0, false, 0);
                check(presenter.pending_command().has_value(),
                      std::string("Application state pointer click did not queue ") +
                          buttons[index] + " at width " + std::to_string(width));
                const auto ack = gate.dispatch(*presenter.pending_command(),
                                               [&](const auto& request) { game.command(request); });
                check(ack.at("ok"), ack.dump());
                presenter.acknowledge(ack);
                snapshot = game.snapshot();
                check(presenter.accept(snapshot), presenter.diagnostic());
                presenter.update(++now, width, height);
                check(application_state_current(game.scene().world(), "app") == modes[index],
                      "Application state pointer control selected the wrong mode");
                if (activity) {
                    // A gated document enters/leaves this snapshot. The normal
                    // presenter may rebuild its context; reacquire borrowed DOM.
                    native = Rml::GetContext(0);
                    document = native ? native->GetDocument(0) : nullptr;
                    check(document && document->GetElementById("panel"),
                          "Activity controls disappeared during document reconciliation");
                } else
                    check(Rml::GetContext(0) == native,
                          "Signal Lock transition changed native context");
                if (!activity)
                    check(application_state_current(game.scene().world(), "lock") ==
                              (index == 2 ? "open" : "closed"),
                          "Signal Lock computed result is wrong");
                check(presenter.application_states_state() == snapshot.at("application_states"),
                      "Displayed state observation differs from actual UI bindings");
                check(game.session().status().at("clock").at("tick") ==
                          (activity && index == 3 ? 1 : 0),
                      "A state control advanced time or Step did not advance exactly once");
                capture(names[index]);
            }
            check(game.scene().snapshot() == original,
                  "Application state pointer controls changed authored scene");
        }
        return frames;
    };
    const auto authored = present(root, source, "authored");
    RuntimeUiInspector inspector = [&](AssetId id, std::stop_token) {
        UiPresenter presenter(renderer.render_interface(), root, font);
        return presenter.inspect_static({id});
    };
    const std::array roots{source.at("asset_id").get<AssetId>()};
    const auto package = scratch / "Export";
    package_runtime_content(root, package, roots, {"windows", "d3d12"}, {}, {}, {}, inspector);
    const auto moved = scratch / "Relocated game";
    std::filesystem::rename(package, moved);
    const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
    const auto packaged_source = read(moved / catalog.records().at(roots[0]).source);
    std::filesystem::rename(root, scratch / "Sources offline");
    const auto exported = present(moved, packaged_source, "relocated");
    check(authored == exported && authored.size() == (activity ? 10 : 12),
          "Relocated source-free Signal Lock changed actual game UI pixels");
    std::ofstream receipt(output /
                          (activity ? "ui-state-conditions-pixels.json" : "ui-states-pixels.json"));
    receipt << Json{{"scene", roots[0]},
                    {"exact_display_pairs", activity ? 10 : 12},
                    {"layouts", {{1000, 900}, {420, 720}}},
                    {"scope",
                     "Actual native application-state UI/GameSession, paused pointer controls "
                     "and relocated source-free full Scene closure; no Windows, full "
                     "editor, physical GPU or graphical EXE acceptance."}}
                   .dump(2);
    check(bool(receipt.flush()), "Signal Lock native pixel receipt failed");
    std::filesystem::remove_all(scratch);
}
} // namespace forge::test_state_gallery_pixels
