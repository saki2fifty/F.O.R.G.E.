#pragma once
#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include <RmlUi/Core.h>
#include <cmath>
#include <cstring>
#include <forge/ui_diligent.hpp>
#include <forge/ui_presenter.hpp>
#include <fstream>
#include <utility>
namespace forge::test_ui_animation {
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class Read, class Save>
void exercise(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
              UiDiligentRenderer& renderer, const std::filesystem::path& output,
              const std::filesystem::path& font_path, const std::filesystem::path& gallery,
              Read&& readback, Save&& save) {
    std::ifstream stream(font_path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream), {}};
    std::vector<std::byte> font(bytes.size());
    std::memcpy(font.data(), bytes.data(), bytes.size());
    const auto catalog = AssetCatalog::open_project(gallery);
    AssetId asset;
    for (const auto& [id, record] : catalog.records())
        if (record.source == "Assets/UI/Motion/hud.rml")
            asset = id;
    check(bool(asset), "Gallery animated UI asset missing");
    for (const auto& layout : {std::pair{800u, 600u}, std::pair{420u, 640u}}) {
        const auto [width, height] = layout;
        Diligent::TextureDesc desc;
        desc.Name = "Animated UI pixel target";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
        Diligent::RefCntAutoPtr<Diligent::ITexture> target;
        device->CreateTexture(desc, nullptr, &target);
        check(bool(target), "Animated UI target unavailable");
        auto* view = target->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        UiPresenter presenter(renderer.render_interface(), gallery, font);
        presenter.reset("animated-pixels", 1);
        const auto entity = EntityId::generate();
        nlohmann::json state{
            {"version", 1},
            {"session", "animated-pixels"},
            {"generation", 1u},
            {"revision", 1u},
            {"documents", nlohmann::json::array({{{"entity", entity},
                                                  {"asset", asset},
                                                  {"instance", entity.str()},
                                                  {"visible", true},
                                                  {"layer", 0u},
                                                  {"model", {{"tick", 24u}, {"paused", true}}},
                                                  {"commands", {"Pause", "Resume", "Step"}}}})}};
        check(presenter.accept(state), presenter.diagnostic().c_str());
        auto* doc = Rml::GetContext(0)->GetDocument(0);
        auto* fill = doc->GetElementById("fill");
        auto* marker = doc->GetElementById("marker");
        for (int i = 1; i <= 15; ++i) {
            presenter.update(i * .05, width, height);
            if (i != 5 && i != 15)
                continue;
            context->SetRenderTargets(1, &view, nullptr,
                                      Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[]{.02f, .025f, .03f, 1};
            context->ClearRenderTarget(view, clear,
                                       Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            renderer.begin(context, view, width, height);
            presenter.render();
            renderer.end();
            check(presenter.diagnostic().empty(), presenter.diagnostic().c_str());
            const auto pixels = readback(device, context, view);
            const auto pixel = [&](Rml::Vector2f origin, int x, int y) {
                const auto px = int(origin.x) + x, py = int(origin.y) + y;
                check(px >= 0 && py >= 0 && px < int(width) && py < int(height),
                      "Animated UI pixel outside viewport");
                return pixels[std::size_t(py) * width + std::size_t(px)];
            };
            const bool first = i == 5;
            check(std::abs(fill->GetProperty<float>("width") - (first ? 90 : 190)) < .02f,
                  "Animated UI layout did not progress");
            const auto fill_position = fill->GetAbsoluteOffset();
            const auto color = pixel(fill_position, 20, 16);
            const float opacity = first ? .4f : .8f;
            const int foreground[]{72, 182, 213}, background[]{16, 29, 41};
            for (int channel = 0; channel != 3; ++channel)
                check(std::abs(color[channel] - (foreground[channel] * opacity +
                                                 background[channel] * (1 - opacity))) <= 2,
                      "Animated UI opacity pixels differ");
            const auto edge = pixel(fill_position, 120, 16);
            check(first ? edge[0] == 16 && edge[1] == 29 : edge[1] > 140,
                  "Animated width did not change actual drawn coverage");
            const auto marker_position = marker->GetAbsoluteOffset();
            const auto moved = pixel(marker_position, (first ? 50 : 150) + 16, 16);
            check(moved[0] == 240 && moved[1] == 180 && moved[2] == 88,
                  "Animated transform did not move actual pixels");
            const auto old = pixel(marker_position, (first ? 150 : 50) + 16, 16);
            check(old[0] == 16 && old[1] == 29 && old[2] == 41,
                  "Animated transform left pixels at the old location");
            save(pixels, width, height,
                 output / ("ui-animation-" + std::to_string(width) +
                           (first ? "-early.ppm" : "-late.ppm")));
        }
    }
}
} // namespace forge::test_ui_animation
