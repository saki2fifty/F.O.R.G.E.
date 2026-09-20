#include "texture_decode.h"
#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct ForgePngReader {
    const unsigned char* source;
    size_t length, offset;
    ForgeImagePixels* result;
    png_bytep* rows;
} ForgePngReader;
static void forge_png_error(png_structp png, png_const_charp message) {
    ForgePngReader* reader = (ForgePngReader*)png_get_error_ptr(png);
    if (reader && reader->result) {
        size_t n = strlen(message);
        if (n >= sizeof(reader->result->error))
            n = sizeof(reader->result->error) - 1;
        memcpy(reader->result->error, message, n);
        reader->result->error[n] = 0;
    }
    png_longjmp(png, 1);
}
static void forge_png_warning(png_structp png, png_const_charp message) {
    (void)png;
    (void)message; // CRC failures are configured as errors, not warnings.
}
static void forge_png_read(png_structp png, png_bytep out, png_size_t size) {
    ForgePngReader* reader = (ForgePngReader*)png_get_io_ptr(png);
    if (!reader || reader->offset > reader->length || size > reader->length - reader->offset)
        png_error(png, "Truncated PNG input");
    memcpy(out, reader->source + reader->offset, size);
    reader->offset += size;
}
void forge_image_pixels_free(ForgeImagePixels* result) {
    if (result) {
        free(result->pixels);
        result->pixels = NULL;
    }
}
int forge_png_decode(const void* bytes, size_t size, unsigned dimension_limit, size_t byte_limit,
                     int header_only, ForgeImagePixels* result) {
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    if (!bytes || size < 8 || png_sig_cmp((png_const_bytep)bytes, 0, 8) || !dimension_limit) {
        strcpy(result->error, "Invalid PNG signature/limit");
        return 0;
    }
    ForgePngReader* reader = (ForgePngReader*)calloc(1, sizeof(*reader));
    if (!reader) {
        strcpy(result->error, "PNG allocation failed");
        return 0;
    }
    reader->source = (const unsigned char*)bytes;
    reader->length = size;
    reader->result = result;
    png_structp png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, reader, forge_png_error, forge_png_warning);
    if (!png) {
        free(reader);
        strcpy(result->error, "PNG initialization failed");
        return 0;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        free(reader);
        strcpy(result->error, "PNG info initialization failed");
        return 0;
    }
    if (setjmp(png_jmpbuf(png))) {
        free(reader->rows);
        forge_image_pixels_free(result);
        png_destroy_read_struct(&png, &info, NULL);
        free(reader);
        return 0;
    }
    png_set_read_fn(png, reader, forge_png_read);
    png_set_user_limits(png, dimension_limit, dimension_limit);
    png_set_chunk_cache_max(png, 1024);
    png_set_chunk_malloc_max(png, byte_limit < 16 * 1024 * 1024 ? byte_limit : 16 * 1024 * 1024);
    png_set_crc_action(png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
    png_read_info(png, info);
    int depth = png_get_bit_depth(png, info), color = png_get_color_type(png, info);
    if (color == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    // Native CPU image consumers use host-endian uint16. Cooked output is LE.
    const uint16_t endian = 1;
    if (depth == 16 && *(const unsigned char*)&endian)
        png_set_swap(png);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    result->width = png_get_image_width(png, info);
    result->height = png_get_image_height(png, info);
    result->channels = png_get_channels(png, info);
    result->component_bytes = png_get_bit_depth(png, info) / 8;
    result->stride = png_get_rowbytes(png, info);
    if (!result->width || !result->height || result->stride > byte_limit / result->height)
        png_error(png, "PNG pixels exceed byte budget");
    result->size = result->stride * result->height;
    if (!header_only) {
        result->pixels = (unsigned char*)malloc(result->size);
        reader->rows = (png_bytep*)malloc(sizeof(png_bytep) * result->height);
        if (!result->pixels || !reader->rows)
            png_error(png, "PNG pixel allocation failed");
        for (unsigned row = 0; row < result->height; ++row)
            reader->rows[row] = result->pixels + (size_t)row * result->stride;
        png_read_image(png, reader->rows);
        png_read_end(png, info);
        if (reader->offset != reader->length)
            png_error(png, "Unexpected bytes after PNG image");
    }
    free(reader->rows);
    png_destroy_read_struct(&png, &info, NULL);
    free(reader);
    return 1;
}
