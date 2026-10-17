#pragma once
#include "asset_bytes.hpp"
#include "presentation_diligent.hpp"
#include "texture_decode.h"
#include <cstring>
namespace forge::test {
// Independent test-only reference. This synchronous read is never used by the
// product image task. Captures already wait for the native device in this fixture.
struct ViewImageReference {
    Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
    unsigned width{}, height{};
    void queue(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
               Diligent::ITextureView* view) {
        using namespace Diligent;
        auto* texture = view->GetTexture();
        auto desc = texture->GetDesc();
        if (!desc.Width || !desc.Height || desc.Width > 4096 || desc.Height > 4096 ||
            desc.Format != TEX_FORMAT_RGBA8_UNORM || desc.MipLevels != 1 || desc.SampleCount != 1)
            throw std::runtime_error("Unexpected native display reference extent/format");
        width = desc.Width;
        height = desc.Height;
        desc.Name = "FORGE independent view image reference";
        desc.Usage = USAGE_STAGING;
        desc.BindFlags = BIND_NONE;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        desc.MiscFlags = MISC_TEXTURE_FLAG_NONE;
        staging.Release();
        device->CreateTexture(desc, nullptr, &staging);
        if (!staging)
            throw std::runtime_error("Native display reference allocation failed");
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        context->CopyTexture({texture, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    }
    std::vector<std::uint8_t> read(Diligent::IDeviceContext* context) const {
        using namespace Diligent;
        if (!staging)
            throw std::runtime_error("Native display reference was not queued");
        std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);
        context->WaitForIdle();
        MappedTextureSubresource data;
        context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr,
                                       data);
        if (!data.pData)
            throw std::runtime_error("Native display reference map failed");
        const auto row = std::size_t(width) * 4;
        const bool valid = data.Stride >= row;
        if (valid)
            for (unsigned y = 0; y < height; ++y)
                std::memcpy(pixels.data() + y * row,
                            static_cast<const std::uint8_t*>(data.pData) + y * data.Stride, row);
        context->UnmapTextureSubresource(staging, 0, 0);
        if (!valid)
            throw std::runtime_error("Native display reference has invalid row pitch");
        return pixels;
    }
};
inline Json verify_saved_view(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                              Diligent::ITextureView* view, const std::filesystem::path& path,
                              const ViewImageReference* before_ui = nullptr) {
    const auto bytes = asset_detail::read_bytes(path, 68 * 1024 * 1024);
    struct Decoded : ForgeImagePixels {
        Decoded() : ForgeImagePixels{} {}
        ~Decoded() { forge_image_pixels_free(this); }
    } decoded;
    if (!forge_png_decode(bytes.data(), bytes.size(), 4096, 64 * 1024 * 1024, false, &decoded))
        throw std::runtime_error(decoded.error);
    ViewImageReference reference;
    reference.queue(device, context, view);
    const auto pixels = reference.read(context);
    if (decoded.width != reference.width || decoded.height != reference.height ||
        decoded.channels != 4 || decoded.component_bytes != 1 || decoded.size != pixels.size() ||
        std::memcmp(decoded.pixels, pixels.data(), pixels.size()) != 0)
        throw std::runtime_error("Saved PNG differs from the actual rendered display image");
    unsigned changed = 0;
    if (before_ui) {
        const auto background = before_ui->read(context);
        if (background.size() != pixels.size() || before_ui->width != reference.width ||
            before_ui->height != reference.height)
            throw std::runtime_error("Game image dimensions changed before UI comparison");
        for (std::size_t at = 0; at < pixels.size(); at += 4)
            changed += std::memcmp(pixels.data() + at, background.data() + at, 4) != 0;
        if (changed < 100)
            throw std::runtime_error("Saved Game PNG does not contain the rendered runtime UI");
    }
    return {{"width", reference.width},
            {"height", reference.height},
            {"matches_view", true},
            {"ui_changed_pixels", changed},
            {"encoded_bytes", bytes.size()}};
}
} // namespace forge::test
