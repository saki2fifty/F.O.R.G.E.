#pragma once
#include "sequence_encoding.hpp"
#include "texture_decode.h"
#include <stdexcept>
#include <utility>

namespace forge::asset_detail {
inline void validate_sequence_snapshots(std::span<const ArtifactFile> inputs, std::string_view name,
                                        std::size_t initial_bytes = 0) {
    if (initial_bytes > sequence_snapshot_byte_limit)
        throw std::runtime_error(std::string(name) + " snapshot exceeds its byte budget");
    std::size_t total = initial_bytes;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        if (input.name != "frame-" + std::to_string(i) + ".png" ||
            input.bytes.size() > FORGE_VIEW_IMAGE_ENCODED_LIMIT ||
            input.bytes.size() > sequence_snapshot_byte_limit - total)
            throw std::runtime_error(std::string(name) +
                                     " source snapshot exceeds its profile or byte budget");
        total += input.bytes.size();
    }
}
struct SequenceFramePixels : ForgeImagePixels {
    SequenceFramePixels() : ForgeImagePixels{} {}
    SequenceFramePixels(const SequenceFramePixels&) = delete;
    SequenceFramePixels(SequenceFramePixels&& other) noexcept
        : ForgeImagePixels(std::exchange(static_cast<ForgeImagePixels&>(other), {})) {}
    ~SequenceFramePixels() { forge_image_pixels_free(this); }
};
inline SequenceFramePixels decode_sequence_frame(std::span<const std::byte> png, unsigned width,
                                                 unsigned height, std::string_view name) {
    SequenceFramePixels pixels;
    if (!forge_png_decode(png.data(), png.size(), FORGE_VIEW_IMAGE_MAX_EXTENT,
                          FORGE_VIEW_IMAGE_PIXEL_LIMIT, 0, &pixels))
        throw std::runtime_error(pixels.error);
    if (pixels.width != width || pixels.height != height || pixels.channels != 4 ||
        pixels.component_bytes != 1 || !pixels.srgb || pixels.stride != std::size_t(width) * 4 ||
        pixels.size != pixels.stride * height)
        throw std::runtime_error(std::string(name) + " requires consistent sRGB RGBA8 PNG frames");
    return pixels;
}
} // namespace forge::asset_detail
