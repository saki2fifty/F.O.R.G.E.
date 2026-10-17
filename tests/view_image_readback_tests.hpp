#pragma once
#include "presentation_diligent.hpp"
#include "texture_decode.h"
#include "texture_encode.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
namespace forge::test_view_image {
inline void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& images) {
    using namespace Diligent;
    const unsigned width = 7, height = 3;
    std::vector<std::uint8_t> expected(width * height * 4);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const auto at = (y * width + x) * 4;
            expected[at] = static_cast<std::uint8_t>(x * 31);
            expected[at + 1] = static_cast<std::uint8_t>(y * 79);
            expected[at + 2] = static_cast<std::uint8_t>(255 - x * 17 - y * 11);
            expected[at + 3] = static_cast<std::uint8_t>(x == y ? 0 : x % 2 ? 128 : 255);
        }
    auto target = [&] {
        TextureDesc desc;
        desc.Name = "FORGE image capture expected pixels";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_SHADER_RESOURCE;
        TextureSubResData row{expected.data(), width * 4};
        TextureData initial{&row, 1};
        RefCntAutoPtr<ITexture> texture;
        presentation.device()->CreateTexture(desc, &initial, &texture);
        check(bool(texture), "Capture test target unavailable");
        return texture;
    };
    auto texture = target();
    presentation.readback(context, texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    check(presentation.readback_pending() && !presentation.take_readback(context),
          "Capture read before its frame was submitted");
    bool refused = false;
    try {
        presentation.readback(context, texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "Concurrent capture replaced the pending image");
    refused = false;
    try {
        (void)presentation.take_readback(nullptr);
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused && presentation.readback_pending(), "Invalid context consumed pending image");
    texture.Release(); // The presentation owner must retain this exact revision.
    TextureDesc resized_desc;
    resized_desc.Name = "FORGE resized view after capture";
    resized_desc.Type = RESOURCE_DIM_TEX_2D;
    resized_desc.Width = 11;
    resized_desc.Height = 5;
    resized_desc.Format = TEX_FORMAT_RGBA8_UNORM;
    resized_desc.BindFlags = BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> replacement;
    presentation.device()->CreateTexture(resized_desc, nullptr, &replacement);
    check(bool(replacement), "Resized view target allocation failed");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto complete = [&] {
        presentation.end_frame(context);
        context->Flush(); // Offscreen test host submission, not part of the capture helper.
        std::optional<DiligentPresentation::DisplayPixels> result;
        do {
            result = presentation.take_readback(context);
            if (result || !presentation.readback_pending())
                break;
            check(std::chrono::steady_clock::now() < deadline, "Capture fence timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
        return result;
    };
    auto result = complete();
    check(result && result->width == width && result->height == height &&
              result->rgba == expected && !presentation.readback_pending(),
          "Native capture changed row order, pitch, channels, alpha or source lifetime");
    ForgeImageBytes encoded{};
    const bool encoded_ok = forge_png_encode_rgba8(result->rgba.data(), result->rgba.size(), width,
                                                   height, width * 4, 1024 * 1024, &encoded);
    const std::string encode_error = encoded.error;
    if (!encoded_ok) {
        forge_image_bytes_free(&encoded);
        throw std::runtime_error(encode_error);
    }
    ForgeImagePixels decoded{};
    const bool decoded_ok =
        forge_png_decode(encoded.bytes, encoded.size, 4096, 64 * 1024 * 1024, false, &decoded);
    const bool equal = decoded_ok && decoded.size == expected.size() &&
                       std::memcmp(decoded.pixels, expected.data(), expected.size()) == 0;
    forge_image_pixels_free(&decoded);
    std::filesystem::create_directories(images);
    std::ofstream output(images / "view-image-rgba.png", std::ios::binary);
    output.write(reinterpret_cast<const char*>(encoded.bytes),
                 static_cast<std::streamsize>(encoded.size));
    forge_image_bytes_free(&encoded);
    check(equal && bool(output.flush()), "Native capture PNG did not preserve expected pixels");
    texture = target();
    presentation.readback(context, texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    presentation.cancel_readback();
    check(presentation.readback_pending(), "Cancelled copy released before frame completion");
    check(!complete() && !presentation.readback_pending(), "Cancelled copy produced pixels");
    presentation.readback(context, texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    const auto again = complete();
    check(again && again->rgba == expected, "Capture did not recover after cancellation");
    resized_desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    RefCntAutoPtr<ITexture> unsupported;
    presentation.device()->CreateTexture(resized_desc, nullptr, &unsupported);
    check(bool(unsupported), "Unsupported capture format fixture unavailable");
    refused = false;
    try {
        presentation.readback(context, unsupported->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused && !presentation.readback_pending(), "Capture accepted a non-display format");
    std::exception_ptr thread_error;
    std::thread other([&] {
        try {
            presentation.cancel_readback();
        } catch (...) {
            thread_error = std::current_exception();
        }
    });
    other.join();
    check(bool(thread_error), "Capture accepted another thread");
}
} // namespace forge::test_view_image
