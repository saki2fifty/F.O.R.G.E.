#pragma once
#include "display_resolve.hpp"
void check_display_resolve(forge::DiligentPresentation& presentation,
                           Diligent::IDeviceContext* context, const std::filesystem::path& images) {
    using namespace Diligent;
    forge::DisplayResolve display(presentation);
    TextureDesc desc;
    desc.Name = "FORGE HDR display fixture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> hdr;
    presentation.device()->CreateTexture(desc, nullptr, &hdr);
    require(bool(hdr), "HDR fixture allocation failed");
    auto render = [&](std::array<float, 4> value, float ev) {
        auto* rtv = hdr->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, value.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return readback(
            presentation.device(), context,
            display.resolve(context, hdr->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), ev));
    };
    auto gray = render({.18f, .18f, .18f, 1}, 0);
    save(gray, 32, 32, images / "hdr-neutral-gray.ppm");
    require(gray[0][0] >= 104 && gray[0][0] <= 106 && gray[0][0] == gray[0][1] &&
                gray[0][1] == gray[0][2],
            "PBR Neutral .18 gray did not map to .14 linear with exactly one sRGB transfer");
    const auto brighter = render({.18f, .18f, .18f, 1}, 1);
    require(brighter[0][0] > gray[0][0] + 30, "Positive exposure did not double incident radiance");
    const auto one = render({1, 1, 1, 1}, 0);
    const auto eight = render({8, 8, 8, 1}, 0);
    save(eight, 32, 32, images / "hdr-eight.ppm");
    require(eight[0][0] > one[0][0] && one[0][0] > 230 && eight[0][0] < 255,
            "HDR values were clamped before tone mapping");
    require(render({0, 0, 0, 1}, 0)[0] == std::array<unsigned char, 4>{0, 0, 0, 255},
            "Black HDR input generated a tone-mapping singularity");

    // The source identity changes when a viewport resizes, even if the same
    // display owner is retained. Rebinding must not sample the original texture.
    auto original = hdr;
    desc.Width = desc.Height = 16;
    presentation.device()->CreateTexture(desc, nullptr, &hdr);
    require(bool(hdr), "Replacement HDR fixture allocation failed");
    const auto red = render({.6f, .02f, .01f, 1}, 0);
    require(red.size() == 16 * 16 && red[0][0] > red[0][1] + 100,
            "Display resolve retained an old HDR binding after resize");
    hdr = original;
    const auto restored = render({.18f, .18f, .18f, 1}, 0);
    require(restored == gray, "Display source binding did not return to the original target");
    auto* previous = display.output();
    bool rejected = false;
    try {
        display.resolve(context, previous, 0);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && display.output() == previous,
            "Non-HDR input was accepted or replaced the previous output");
    rejected = false;
    try {
        display.resolve(context, hdr->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                        std::numeric_limits<float>::quiet_NaN());
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && display.output() == previous, "Invalid exposure modified display output");
}
