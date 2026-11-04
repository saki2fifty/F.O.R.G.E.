#include "texture_encode.h"
#include "texture_encode_buffer.h"
#include "view_image_limits.h"
#include <cstring>

// Original approved pinned dependency; only this adapter is FORGE-authored.
// Keep symbols private and file access outside the codec. The header supports
// static-inline API linkage and requests no strict-aliasing optimization.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBIWDEF static inline
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace {
struct JpegOutput {
    ForgeImageWriter writer;
    bool failed = false;
};
void write_jpeg(void* context, void* bytes, int length) {
    auto& output = *static_cast<JpegOutput*>(context);
    if (output.failed)
        return;
    if (length < 0 || !forge_image_writer_append(&output.writer, bytes, std::size_t(length)))
        output.failed = true;
}
} // namespace
int forge_jpeg_encode_rgba8(const void* pixels, std::size_t extent, unsigned width, unsigned height,
                            std::size_t stride, int quality, std::size_t byte_limit,
                            ForgeImageBytes* result) {
    if (!result)
        return 0;
    std::memset(result, 0, sizeof(*result));
    if (!pixels || !width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || stride != std::size_t(width) * 4 ||
        extent != stride * height || extent > FORGE_VIEW_IMAGE_PIXEL_LIMIT ||
        quality < FORGE_JPEG_QUALITY_MIN || quality > FORGE_JPEG_QUALITY_MAX || !byte_limit ||
        byte_limit > FORGE_VIEW_IMAGE_ENCODED_LIMIT) {
        std::strcpy(result->error, "Invalid packed RGBA8 extent, JPEG quality or byte budget");
        return 0;
    }
    const auto* source = static_cast<const unsigned char*>(pixels);
    for (std::size_t at = 3; at < extent; at += 4)
        if (source[at] != 255) {
            std::strcpy(result->error, "JPEG requires opaque alpha; choose PNG or WebP");
            return 0;
        }
    JpegOutput output{{result, 0, byte_limit, "JPEG"}};
    const int encoded =
        stbi_write_jpg_to_func(write_jpeg, &output, int(width), int(height), 4, pixels, quality);
    if (!encoded || output.failed) {
        if (!result->error[0])
            std::strcpy(result->error, "JPEG encoding failed");
        forge_image_bytes_free(result);
        return 0;
    }
    return 1;
}
