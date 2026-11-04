#include "texture_encode.h"
#include "texture_encode_buffer.h"
#include "view_image_limits.h"
#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static void forge_png_write_error(png_structp png, png_const_charp message) {
    ForgeImageWriter* writer = (ForgeImageWriter*)png_get_error_ptr(png);
    if (writer && writer->result && message != writer->result->error) {
        size_t length = strlen(message);
        if (length >= sizeof(writer->result->error))
            length = sizeof(writer->result->error) - 1;
        memcpy(writer->result->error, message, length);
        writer->result->error[length] = 0;
    }
    png_longjmp(png, 1);
}
static void forge_png_write_warning(png_structp png, png_const_charp message) {
    (void)png;
    (void)message;
}
static void forge_png_write(png_structp png, png_bytep bytes, png_size_t length) {
    ForgeImageWriter* writer = (ForgeImageWriter*)png_get_io_ptr(png);
    if (!forge_image_writer_append(writer, bytes, length))
        png_error(png, writer->result->error);
}
void forge_image_bytes_free(ForgeImageBytes* result) {
    if (result) {
        free(result->bytes);
        result->bytes = NULL;
        result->size = 0;
    }
}
static int forge_png_encode(const void* pixels, size_t extent, unsigned width, unsigned height,
                            size_t stride, unsigned depth, int srgb, size_t byte_limit,
                            ForgeImageBytes* result) {
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    const size_t channels_bytes = depth == 16 ? 8 : 4;
    const size_t pixel_limit =
        depth == 16 ? FORGE_UINT16_IMAGE_PIXEL_LIMIT : FORGE_VIEW_IMAGE_PIXEL_LIMIT;
    const size_t encoded_limit =
        depth == 16 ? FORGE_UINT16_IMAGE_ENCODED_LIMIT : FORGE_VIEW_IMAGE_ENCODED_LIMIT;
    const size_t row_bytes = (size_t)width * channels_bytes;
    if ((depth != 8 && depth != 16) || (srgb != 0 && srgb != 1) || !pixels || !width || !height ||
        width > FORGE_VIEW_IMAGE_MAX_EXTENT || height > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        stride < row_bytes || extent > pixel_limit || !byte_limit || byte_limit > encoded_limit ||
        (height > 1 && stride > (SIZE_MAX - row_bytes) / (height - 1)) ||
        (size_t)(height - 1) * stride + row_bytes > extent) {
        strcpy(result->error, "Invalid RGBA image extent, depth or PNG byte budget");
        return 0;
    }
    ForgeImageWriter* writer = (ForgeImageWriter*)calloc(1, sizeof(*writer));
    if (!writer) {
        strcpy(result->error, "PNG writer allocation failed");
        return 0;
    }
    writer->result = result;
    writer->limit = byte_limit;
    writer->codec = "PNG";
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, writer, forge_png_write_error,
                                              forge_png_write_warning);
    if (!png) {
        free(writer);
        strcpy(result->error, "PNG writer initialization failed");
        return 0;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, NULL);
        free(writer);
        strcpy(result->error, "PNG writer info initialization failed");
        return 0;
    }
    if (setjmp(png_jmpbuf(png))) {
        forge_image_bytes_free(result);
        png_destroy_write_struct(&png, &info);
        free(writer);
        return 0;
    }
    png_set_write_fn(png, writer, forge_png_write, NULL);
    png_set_IHDR(png, info, width, height, (int)depth, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    if (srgb)
        png_set_sRGB(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    png_set_compression_level(png, 3);
    png_write_info(png, info);
    // The DTO/C boundary supplies native uint16. PNG always stores high byte first.
    const uint16_t endian = 1;
    if (depth == 16 && *(const unsigned char*)&endian)
        png_set_swap(png);
    for (unsigned row = 0; row < height; ++row)
        png_write_row(png, (png_const_bytep)pixels + (size_t)row * stride);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    free(writer);
    return 1;
}

int forge_png_encode_rgba8_profile(const void* pixels, size_t extent, unsigned width,
                                   unsigned height, size_t stride, int srgb, size_t byte_limit,
                                   ForgeImageBytes* result) {
    return forge_png_encode(pixels, extent, width, height, stride, 8, srgb, byte_limit, result);
}
int forge_png_encode_rgba16(const void* pixels, size_t extent, unsigned width, unsigned height,
                            size_t stride, size_t byte_limit, ForgeImageBytes* result) {
    return forge_png_encode(pixels, extent, width, height, stride, 16, 0, byte_limit, result);
}
int forge_png_encode_rgba8(const void* pixels, size_t extent, unsigned width, unsigned height,
                           size_t stride, size_t byte_limit, ForgeImageBytes* result) {
    return forge_png_encode_rgba8_profile(pixels, extent, width, height, stride, 1, byte_limit,
                                          result);
}
