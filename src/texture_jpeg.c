#include "texture_decode.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
// Private symbols; reuse the exact stb source already pinned by DiligentTools.
// No vendored edit, new decoder version or collision with its HDR/TGA instance.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"
int forge_jpeg_decode(const void* bytes, size_t size, unsigned dimension_limit, size_t byte_limit,
                      int header_only, ForgeImagePixels* result) {
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    const unsigned char* source = (const unsigned char*)bytes;
    if (!bytes || size < 4 || size > INT_MAX || !dimension_limit || source[0] != 255 ||
        source[1] != 216 || source[size - 2] != 255 || source[size - 1] != 217) {
        strcpy(result->error, "Invalid/truncated JPEG framing");
        return 0;
    }
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(source, (int)size, &width, &height, &channels) || width <= 0 ||
        height <= 0 || (unsigned)width > dimension_limit || (unsigned)height > dimension_limit ||
        (channels != 1 && channels != 3)) {
        strcpy(result->error, "Unsupported JPEG header/dimensions");
        return 0;
    }
    result->width = (unsigned)width;
    result->height = (unsigned)height;
    result->channels = (unsigned)channels;
    result->component_bytes = 1;
    result->stride = (size_t)width * (unsigned)channels;
    if (result->stride > byte_limit / (unsigned)height) {
        strcpy(result->error, "JPEG pixels exceed byte budget");
        return 0;
    }
    result->size = result->stride * (unsigned)height;
    if (!header_only) {
        result->pixels = stbi_load_from_memory(source, (int)size, &width, &height, &channels,
                                               (int)result->channels);
        if (!result->pixels) {
            const char* why = stbi_failure_reason();
            if (!why)
                why = "JPEG decode failed";
            size_t n = strlen(why);
            if (n >= sizeof(result->error))
                n = sizeof(result->error) - 1;
            memcpy(result->error, why, n);
            result->error[n] = 0;
            return 0;
        }
        if ((unsigned)width != result->width || (unsigned)height != result->height ||
            (unsigned)channels != result->channels) {
            forge_image_pixels_free(result);
            strcpy(result->error, "JPEG decode/header mismatch");
            return 0;
        }
    }
    return 1;
}
