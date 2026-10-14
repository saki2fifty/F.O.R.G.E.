#pragma once
#include <cstddef>
#include <png.h>
#include <stdexcept>
#include <vector>
namespace forge::test {
// FORGE-owned fixture bytes, encoded through pinned libpng's public simplified API.
inline std::vector<std::byte> encode_ui_png(const void* pixels, unsigned width, unsigned height,
                                            png_uint_32 format = PNG_FORMAT_RGBA) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = format;
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, pixels, 0, nullptr))
        throw std::runtime_error("Owned PNG sizing failed");
    std::vector<std::byte> bytes(size);
    if (!png_image_write_to_memory(&image, bytes.data(), &size, 0, pixels, 0, nullptr))
        throw std::runtime_error("Owned PNG encoding failed");
    bytes.resize(size);
    png_image_free(&image);
    return bytes;
}
} // namespace forge::test
