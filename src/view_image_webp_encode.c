#include "texture_encode_buffer.h"
#include "view_image_limits.h"
#include <limits.h>
#include <stdint.h>
#include <webp/encode.h>

static int forge_webp_write(const uint8_t* bytes, size_t size, const WebPPicture* picture) {
    return forge_image_writer_append((ForgeImageWriter*)picture->custom_ptr, bytes, size);
}
int forge_webp_encode_rgba8(const void* pixels, size_t extent, unsigned width, unsigned height,
                            size_t stride, size_t byte_limit, ForgeImageBytes* result) {
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    if (!pixels || !width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || stride < (size_t)width * 4 || stride > INT_MAX ||
        extent > FORGE_VIEW_IMAGE_PIXEL_LIMIT || !byte_limit ||
        byte_limit > FORGE_VIEW_IMAGE_ENCODED_LIMIT ||
        (height > 1 && stride > (SIZE_MAX - (size_t)width * 4) / (height - 1)) ||
        (size_t)(height - 1) * stride + (size_t)width * 4 > extent) {
        strcpy(result->error, "Invalid RGBA8 image extent or WebP byte budget");
        return 0;
    }
    WebPConfig config;
    WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPConfigLosslessPreset(&config, 4) ||
        !WebPPictureInit(&picture)) {
        strcpy(result->error, "WebP encoder initialization failed");
        return 0;
    }
    config.exact = 1;
    config.thread_level = 0;
    if (!WebPValidateConfig(&config)) {
        strcpy(result->error, "WebP encoder configuration rejected");
        return 0;
    }
    ForgeImageWriter writer = {result, 0, byte_limit, "WebP"};
    picture.width = (int)width;
    picture.height = (int)height;
    picture.use_argb = 1;
    picture.writer = forge_webp_write;
    picture.custom_ptr = &writer;
    const int imported = WebPPictureImportRGBA(&picture, (const uint8_t*)pixels, (int)stride);
    const int succeeded = imported && WebPEncode(&config, &picture);
    const int error = picture.error_code;
    WebPPictureFree(&picture);
    if (!succeeded) {
        if (!result->error[0])
            snprintf(result->error, sizeof(result->error), "WebP %s failed (native code %d)",
                     imported ? "encoding" : "pixel import", error);
        forge_image_bytes_free(result);
        return 0;
    }
    return 1;
}
