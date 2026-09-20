#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ForgeImagePixels {
    unsigned width, height, channels, component_bytes;
    size_t stride, size;
    unsigned char* pixels;
    char error[256];
} ForgeImagePixels;
// C boundaries contain codec longjmp; never cross C++ object lifetimes.
int forge_png_decode(const void* bytes, size_t size, unsigned dimension_limit, size_t byte_limit,
                     int header_only, ForgeImagePixels* result);
int forge_jpeg_decode(const void* bytes, size_t size, unsigned dimension_limit, size_t byte_limit,
                      int header_only, ForgeImagePixels* result);
void forge_image_pixels_free(ForgeImagePixels* result);
#ifdef __cplusplus
}
#endif
