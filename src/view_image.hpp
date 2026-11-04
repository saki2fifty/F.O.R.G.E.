#pragma once
#include "image_pixels.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace forge {
enum class ViewImageFormat { Png, Webp, Hdr };
ViewImageFormat view_image_format(const std::filesystem::path&);
const char* view_image_suffix(ViewImageFormat);
const char* view_image_mime(ViewImageFormat);
void save_view_image(const std::filesystem::path&, unsigned width, unsigned height,
                     std::span<const std::uint8_t> rgba);
// PNG retains the producer's explicit sRGB label; unlabelled stored channels use
// PNG. The supported WebP output profile requires sRGB bytes.
void save_image_pixels(const std::filesystem::path&, const Rgba8Pixels&);
// Quantized RGBE .hdr only; opaque linear HDR values have no alpha or inline preview.
void save_image_pixels(const std::filesystem::path&, const Rgb32Pixels&);
void save_image_pixels(const std::filesystem::path&, const ImagePixels&);
// Detached display pixels; no device, Scene, catalog or project-writer ownership.
void save_view_png(const std::filesystem::path&, unsigned width, unsigned height,
                   std::span<const std::uint8_t> rgba);
// Detached, bounded protocol preview of the same display pixels. Full PNG output
// stays at its requested resolution. Heavy codec/resampling work belongs in IO jobs.
struct ViewImagePreview {
    unsigned source_width = 0, source_height = 0, width = 0, height = 0;
    std::vector<std::byte> png;
};
ViewImagePreview preview_view_pixels(unsigned width, unsigned height,
                                     std::span<const std::uint8_t> rgba, bool srgb = true);
ViewImagePreview preview_image_pixels(const Rgba8Pixels&);
ViewImagePreview preview_image_pixels(const ImagePixels&);
ViewImagePreview read_view_preview(const std::filesystem::path&);
} // namespace forge
