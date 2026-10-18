#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ForgeImageBytes {
    unsigned char* bytes;
    size_t size;
    char error[256];
} ForgeImageBytes;
// Top-to-bottom display-encoded RGBA8. The supplied extent includes any row padding.
// C contains all libpng longjmp; output is owned by the caller until freed below.
int forge_png_encode_rgba8(const void* pixels, size_t extent, unsigned width, unsigned height,
                           size_t stride, size_t byte_limit, ForgeImageBytes* result);
void forge_image_bytes_free(ForgeImageBytes* result);
#ifdef __cplusplus
}
#endif
