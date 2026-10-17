#include "texture_encode.h"
#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct ForgePngWriter {
    ForgeImageBytes* result;
    size_t capacity, limit;
} ForgePngWriter;
static void forge_png_write_error(png_structp png, png_const_charp message) {
    ForgePngWriter* writer = (ForgePngWriter*)png_get_error_ptr(png);
    if (writer && writer->result) {
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
    ForgePngWriter* writer = (ForgePngWriter*)png_get_io_ptr(png);
    ForgeImageBytes* result = writer->result;
    if (length > writer->limit - result->size)
        png_error(png, "PNG output exceeds byte budget");
    const size_t required = result->size + length;
    if (required > writer->capacity) {
        size_t capacity = writer->capacity ? writer->capacity : 4096;
        if (capacity > writer->limit)
            capacity = writer->limit;
        while (capacity < required)
            capacity = capacity > writer->limit / 2 ? writer->limit : capacity * 2;
        void* grown = realloc(result->bytes, capacity);
        if (!grown)
            png_error(png, "PNG output allocation failed");
        result->bytes = (unsigned char*)grown;
        writer->capacity = capacity;
    }
    memcpy(result->bytes + result->size, bytes, length);
    result->size = required;
}
void forge_image_bytes_free(ForgeImageBytes* result) {
    if (result) {
        free(result->bytes);
        result->bytes = NULL;
        result->size = 0;
    }
}
int forge_png_encode_rgba8(const void* pixels, size_t extent, unsigned width, unsigned height,
                           size_t stride, size_t byte_limit, ForgeImageBytes* result) {
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    // Bounds match current editor render targets and include worst-case PNG overhead.
    if (!pixels || !width || !height || width > 4096 || height > 4096 ||
        stride < (size_t)width * 4 || extent > (size_t)64 * 1024 * 1024 || !byte_limit ||
        byte_limit > (size_t)68 * 1024 * 1024 ||
        (height > 1 && stride > (SIZE_MAX - (size_t)width * 4) / (height - 1)) ||
        (size_t)(height - 1) * stride + (size_t)width * 4 > extent) {
        strcpy(result->error, "Invalid RGBA8 image extent or PNG byte budget");
        return 0;
    }
    ForgePngWriter* writer = (ForgePngWriter*)calloc(1, sizeof(*writer));
    if (!writer) {
        strcpy(result->error, "PNG writer allocation failed");
        return 0;
    }
    writer->result = result;
    writer->limit = byte_limit;
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
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_sRGB(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    png_set_compression_level(png, 3);
    png_write_info(png, info);
    for (unsigned row = 0; row < height; ++row)
        png_write_row(png, (png_const_bytep)pixels + (size_t)row * stride);
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    free(writer);
    return 1;
}
