#pragma once
#include "display_resolve.hpp"
#include "transmission_background.hpp"
void check_transmission_background(forge::DiligentPresentation& presentation,
                                   Diligent::IDeviceContext* context) {
    using namespace forge;
    using namespace Diligent;
    // Half-float .5 primaries remain below native PBR Neutral highlight
    // compression; unlike 1.0 primaries they do not intentionally desaturate.
    // .5 linear is 188 (allow one UNORM step) after the single sRGB transfer.
    std::vector<std::array<std::uint16_t, 4>> pixels(64 * 32);
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 64; ++x)
            pixels[y * 64 + x] = x < 32 ? std::array<std::uint16_t, 4>{0x3800, 0, 0, 0x3c00}
                                        : std::array<std::uint16_t, 4>{0, 0, 0x3800, 0x3c00};
    TextureDesc desc;
    desc.Name = "FORGE transmission camera crop fixture";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 64;
    desc.Height = 32;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Usage = USAGE_IMMUTABLE;
    TextureSubResData subresource{pixels.data(), 64 * 8};
    Diligent::TextureData data{&subresource, 1};
    RefCntAutoPtr<ITexture> source;
    presentation.device()->CreateTexture(desc, &data, &source);
    require(bool(source), "Transmission source allocation failed");
    TransmissionBackground snapshot(presentation, 64 * 1024);
    DisplayResolve display(presentation);
    auto capture = [&](PixelViewport area) {
        const auto result = snapshot.capture(context, source, area);
        require(result.viewport == area && result.background == snapshot.output(),
                "Transmission capture lost the camera rectangle");
        return readback(presentation.device(), context,
                        display.resolve(context, result.background, 0));
    };
    const auto left = capture({0, 0, 32, 32});
    require(left.size() == 32 * 32 && left.front()[0] >= 187 && left.front()[0] <= 189 &&
                left.front()[2] < 10 && left.back() == left.front() &&
                snapshot.output()->GetTexture()->GetDesc().MipLevels == 6,
            "Transmission crop included another camera or omitted native mipmaps");
    TextureViewDesc last_view;
    last_view.ViewType = TEXTURE_VIEW_SHADER_RESOURCE;
    last_view.MostDetailedMip = 5;
    last_view.NumMipLevels = 1;
    RefCntAutoPtr<ITextureView> last_mip;
    snapshot.output()->GetTexture()->CreateView(last_view, &last_mip);
    require(bool(last_mip), "Transmission last-mip view is unavailable");
    const auto filtered =
        readback(presentation.device(), context, display.resolve(context, last_mip, 0));
    require(filtered.size() == 1 && filtered.front()[0] >= 187 && filtered.front()[0] <= 189 &&
                filtered.front()[2] < 10,
            "Native transmission mip generation did not populate the last level");
    auto* first = snapshot.output()->GetTexture();
    const auto right = capture({32, 0, 32, 32});
    require(right.front()[2] >= 187 && right.front()[2] <= 189 && right.front()[0] < 10 &&
                right.back() == right.front() && snapshot.output()->GetTexture() == first,
            "Transmission snapshot did not refresh the reused texture");
    const auto resized_pixels = capture({0, 0, 16, 16});
    require(resized_pixels.size() == 16 * 16 && resized_pixels.front()[0] >= 187,
            "Transmission snapshot resize retained old source pixels");
    const auto previous = snapshot.output();
    bool rejected = false;
    try {
        snapshot.capture(context, source, {63, 0, 2, 32});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && snapshot.output() == previous,
            "Invalid camera crop replaced a usable transmission snapshot");
    rejected = false;
    try {
        snapshot.capture(context, previous->GetTexture(), {0, 0, 16, 16});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Transmission pass accepted read/write feedback");
    TransmissionBackground limited(presentation, 1);
    rejected = false;
    try {
        limited.capture(context, source, {0, 0, 32, 32});
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected && !limited.output(), "Transmission allocation bypassed the payload budget");
}
