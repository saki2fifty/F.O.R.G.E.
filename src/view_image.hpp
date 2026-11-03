#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace forge {
enum class ViewImageFormat { Png, Webp };
ViewImageFormat view_image_format(const std::filesystem::path&);
const char* view_image_suffix(ViewImageFormat);
const char* view_image_mime(ViewImageFormat);
void save_view_image(const std::filesystem::path&, unsigned width, unsigned height,
                     std::span<const std::uint8_t> rgba);
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
                                     std::span<const std::uint8_t> rgba);
ViewImagePreview read_view_preview(const std::filesystem::path&);
} // namespace forge
