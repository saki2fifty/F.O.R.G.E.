#pragma once
#include "asset_bytes.hpp"
#include "ui_render_readback.hpp"
#include "view_image.hpp"
#include <RmlUi/Core.h>
#include <forge/authoring.hpp>
#include <forge/runtime_ui.hpp>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>

namespace forge::test_ui_forms {
template <class Read>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& output,
              const std::filesystem::path& font_path, const std::filesystem::path& gallery,
              Read read) {
    const auto require = [](bool value, const char* why) {
        if (!value)
            throw std::runtime_error(why);
    };
    const auto font = asset_detail::read_bytes(font_path, 4 * 1024 * 1024);
    Json source;
    std::ifstream(gallery / "Scenes/212-ui-form-controls.scene.json") >> source;
    EngineContext engine(WorldRole::Runtime, false, {ui_module(gallery)});
    Scene scene(engine.world());
    scene.restore_snapshot(source);
    const auto authored = scene.snapshot();
    const auto ui = std::static_pointer_cast<UiRuntime>(engine.services().ui());
    const auto state = ui->snapshot(scene, "native-form-pixels", 1, 1, true);
    const auto instance = state.at("documents")[0].at("instance").get<std::string>();
    for (const unsigned width : {1180u, 700u, 400u}) {
        constexpr unsigned height = 900;
        Diligent::TextureDesc desc;
        desc.Name = "FORGE native form controls";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
        Diligent::RefCntAutoPtr<Diligent::ITexture> target;
        device->CreateTexture(desc, nullptr, &target);
        require(bool(target), "Native form render target unavailable");
        auto* view = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        UiPresenter presenter(renderer.render_interface(), gallery, font);
        presenter.reset("native-form-pixels", 1);
        require(presenter.accept(state), "Native form snapshot rejected");
        presenter.update(0, width, height);
        auto* document = Rml::GetContext(0)->GetDocument(0);
        auto* panel = document->GetElementById("panel");
        const auto origin = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = panel->GetBox().GetSize(Rml::BoxArea::Border);
        require(origin.x >= 0 && origin.y >= 0 && origin.x + size.x <= width + .5f &&
                    origin.y + size.y <= height + .5f,
                "Form panel escaped its viewport");
        const auto capture = [&](const char* name) {
            context->SetRenderTargets(1, &view, nullptr,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float background[]{.02f, .03f, .04f, 1};
            context->ClearRenderTarget(view, background,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, view, width, height);
            presenter.render();
            renderer.end();
            auto pixels = read(device, context, view);
            require(pixels.size() == std::size_t(width) * height, "Form readback dimensions");
            const auto path =
                output / (std::string("ui-forms-") + name + "-" + std::to_string(width) + ".png");
            save_view_image(path, width, height,
                            {reinterpret_cast<const std::uint8_t*>(pixels.data()),
                             pixels.size() * sizeof(pixels[0])});
            return pixels;
        };
        const auto original = capture("initial");
        const auto revision = presenter.presentation_revision();
        presenter.form_control_action(instance, "music", revision, "check", false);
        presenter.form_control_action(instance, "expert", revision, "choose");
        const auto options = presenter.form_options(instance, "quality");
        presenter.form_control_action(instance, "quality", revision, "select", false, 1,
                                      options.at("options")[1].at("state"));
        presenter.update(.1, width, height);
        require(capture("changed") != original, "Native form actions did not change pixels");
        document->GetElementById("quality")->Click();
        presenter.update(.2, width, height);
        capture("options");
    }
    require(scene.snapshot() == authored, "Native form capture changed authored Scene");
}
} // namespace forge::test_ui_forms
