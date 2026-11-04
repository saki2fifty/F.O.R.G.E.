#include "texture_decode.h"
#include "texture_encode.h"
#include "webp_sequence.hpp"
#include <memory>
#include <stdexcept>
#include <webp/mux.h>

namespace forge::asset_detail {
std::vector<ArtifactFile> execute_webp_sequence_recipe(ImportProcessRequest request,
                                                       std::stop_token stop) {
    auto require = [](bool value, const char* reason) {
        if (!value)
            throw std::runtime_error(reason);
    };
    auto cancelled = [&] { require(!stop.stop_requested(), "WebP animation cancelled"); };
    cancelled();
    const auto& payload = request.payload;
    require(payload.is_object() && payload.size() == 5 &&
                payload.at("recipe") == webp_sequence_recipe &&
                payload.at("profile") == webp_sequence_profile,
            "Invalid WebP animation worker profile");
    auto dimension = [&](const char* key) {
        const auto& value = payload.at(key);
        require(value.is_number_unsigned() && value.get<std::uint64_t>() &&
                    value.get<std::uint64_t>() <= FORGE_VIEW_IMAGE_MAX_EXTENT,
                "WebP animation axes must be between 1 and 4096");
        return value.get<unsigned>();
    };
    const auto width = dimension("width"), height = dimension("height");
    const auto& timing = payload.at("durations_ms");
    require(timing.is_array() && timing.size() >= 2 &&
                timing.size() <= FORGE_SEQUENCE_FRAME_MAX_COUNT &&
                timing.size() == request.inputs.size(),
            "Invalid WebP animation frame count");
    std::vector<unsigned> durations;
    for (const auto& value : timing) {
        require(value.is_number_unsigned() && value.get<std::uint64_t>() <= 0xffffffu,
                "Invalid WebP animation frame duration");
        durations.push_back(value.get<unsigned>());
    }
    (void)validate_webp_sequence_durations(durations);
    std::size_t source_bytes = 0;
    for (std::size_t i = 0; i < request.inputs.size(); ++i) {
        const auto& input = request.inputs[i];
        require(input.name == "frame-" + std::to_string(i) + ".png" &&
                    input.bytes.size() <= FORGE_VIEW_IMAGE_ENCODED_LIMIT &&
                    input.bytes.size() <= webp_sequence_source_limit - source_bytes,
                "WebP animation source snapshot exceeds its profile or byte budget");
        source_bytes += input.bytes.size();
    }
    const std::unique_ptr<WebPMux, decltype(&WebPMuxDelete)> mux(WebPMuxNew(), WebPMuxDelete);
    require(bool(mux), "WebP animation mux initialization failed");
    require(WebPMuxSetCanvasSize(mux.get(), int(width), int(height)) == WEBP_MUX_OK,
            "WebP animation canvas rejected");
    const WebPMuxAnimParams animation{0, 0}; // Transparent background, infinite loop.
    require(WebPMuxSetAnimationParams(mux.get(), &animation) == WEBP_MUX_OK,
            "WebP animation loop settings rejected");
    std::size_t compressed_bytes = 0;
    for (std::size_t i = 0; i < request.inputs.size(); ++i) {
        cancelled();
        auto png = std::move(request.inputs[i].bytes);
        struct Decoded : ForgeImagePixels {
            Decoded() : ForgeImagePixels{} {}
            ~Decoded() { forge_image_pixels_free(this); }
        } pixels;
        require(forge_png_decode(png.data(), png.size(), FORGE_VIEW_IMAGE_MAX_EXTENT,
                                 FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &pixels),
                pixels.error);
        require(pixels.width == width && pixels.height == height && pixels.channels == 4 &&
                    pixels.component_bytes == 1 && pixels.srgb &&
                    pixels.stride == std::size_t(width) * 4 &&
                    pixels.size == pixels.stride * height,
                "WebP animation requires consistent sRGB RGBA8 PNG frames");
        struct Encoded : ForgeImageBytes {
            Encoded() : ForgeImageBytes{} {}
            ~Encoded() { forge_image_bytes_free(this); }
        } frame;
        require(compressed_bytes < webp_sequence_frame_bytes_limit,
                "WebP animation frame data exceeds64 MiB encoded budget");
        require(forge_webp_encode_rgba8(pixels.pixels, pixels.size, width, height, pixels.stride,
                                        webp_sequence_frame_bytes_limit - compressed_bytes, &frame),
                frame.error);
        compressed_bytes += frame.size;
        cancelled();
        WebPMuxFrameInfo info{};
        info.bitstream = {frame.bytes, frame.size};
        info.duration = int(durations[i]);
        info.id = WEBP_CHUNK_ANMF;
        info.dispose_method = WEBP_MUX_DISPOSE_NONE;
        info.blend_method = WEBP_MUX_NO_BLEND;
        require(WebPMuxPushFrame(mux.get(), &info, 1) == WEBP_MUX_OK,
                "WebP animation frame mux failed");
    }
    cancelled();
    struct Assembled : WebPData {
        Assembled() : WebPData{} {}
        ~Assembled() { WebPDataClear(this); }
    } output;
    require(WebPMuxAssemble(mux.get(), &output) == WEBP_MUX_OK, "WebP animation assembly failed");
    require(output.size && output.size <= FORGE_VIEW_IMAGE_ENCODED_LIMIT,
            "WebP animation exceeds68 MiB encoded output");
    cancelled();
    const auto* first = reinterpret_cast<const std::byte*>(output.bytes);
    return {{"animation.webp", {first, first + output.size}}};
}
} // namespace forge::asset_detail
