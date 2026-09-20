#include "texture_decode.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>
#include <webp/demux.h>

static unsigned read32(const uint8_t* p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
int forge_webp_decode(const void* bytes, size_t size, unsigned dimension_limit, size_t byte_limit,
                      int header_only, ForgeImagePixels* result) {
    if (!result)
        return 0;
    memset(result, 0, sizeof(*result));
    const uint8_t* source = (const uint8_t*)bytes;
    if (!source || size < 20 || size > byte_limit || memcmp(source, "RIFF", 4) ||
        memcmp(source + 8, "WEBP", 4) || read32(source + 4) != size - 8) {
        strcpy(result->error, "Invalid/truncated WebP RIFF framing");
        return 0;
    }
    size_t at = 12;
    unsigned count = 0;
    while (at < size) {
        if (size - at < 8 || ++count > 4096) {
            strcpy(result->error, "WebP chunk header/count exceeds bounds");
            return 0;
        }
        const size_t length = read32(source + at + 4);
        if (length > size - at - 8 || (length & 1) > size - at - 8 - length) {
            strcpy(result->error, "Truncated WebP chunk");
            return 0;
        }
        if (!memcmp(source + at, "ICCP", 4)) {
            strcpy(result->error, "WebP ICC color profile requires explicit conversion");
            return 0;
        }
        at += 8 + length + (length & 1);
    }
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config) ||
        WebPGetFeatures(source, size, &config.input) != VP8_STATUS_OK || config.input.width <= 0 ||
        config.input.height <= 0 || (unsigned)config.input.width > dimension_limit ||
        (unsigned)config.input.height > dimension_limit || config.input.has_animation ||
        (size_t)config.input.width * 4 > byte_limit / (unsigned)config.input.height ||
        config.input.width > INT_MAX / 4) {
        strcpy(result->error, "Unsupported WebP header, dimensions, animation or byte budget");
        return 0;
    }
    WebPData data = {source, size};
    WebPDemuxer* demux = WebPDemux(&data);
    const int valid =
        demux && WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT) == 1 &&
        WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH) == (unsigned)config.input.width &&
        WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT) == (unsigned)config.input.height &&
        !(WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS) & (ANIMATION_FLAG | ICCP_FLAG));
    if (demux)
        WebPDemuxDelete(demux);
    if (!valid) {
        strcpy(result->error, "Invalid WebP container/metadata");
        return 0;
    }
    result->width = (unsigned)config.input.width;
    result->height = (unsigned)config.input.height;
    result->channels = 4;
    result->component_bytes = 1;
    result->stride = (size_t)result->width * 4;
    result->size = result->stride * result->height;
    if (header_only)
        return 1;
    result->pixels = (unsigned char*)malloc(result->size);
    if (!result->pixels) {
        strcpy(result->error, "WebP output allocation failed");
        return 0;
    }
    config.output.colorspace = MODE_RGBA;
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = result->pixels;
    config.output.u.RGBA.stride = (int)result->stride;
    config.output.u.RGBA.size = result->size;
    config.options.use_threads = 0;
    const int decoded =
        WebPValidateDecoderConfig(&config) && WebPDecode(source, size, &config) == VP8_STATUS_OK &&
        config.output.width == (int)result->width && config.output.height == (int)result->height;
    WebPFreeDecBuffer(&config.output);
    if (!decoded) {
        forge_image_pixels_free(result);
        strcpy(result->error, "WebP pixel decode failed");
        return 0;
    }
    return 1;
}
