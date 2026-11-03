#pragma once
#include "texture_encode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Shared output storage for native codec callbacks. Codecs keep their own
// failure/cleanup mechanism; no exception crosses either C callback boundary.
typedef struct ForgeImageWriter {
    ForgeImageBytes* result;
    size_t capacity, limit;
    const char* codec;
} ForgeImageWriter;
static inline int forge_image_writer_append(ForgeImageWriter* writer, const void* bytes,
                                            size_t length) {
    ForgeImageBytes* result = writer->result;
    if (result->size > writer->limit || length > writer->limit - result->size) {
        snprintf(result->error, sizeof(result->error), "%s output exceeds byte budget",
                 writer->codec);
        return 0;
    }
    if (!length)
        return 1;
    const size_t required = result->size + length;
    if (required > writer->capacity) {
        size_t capacity = writer->capacity ? writer->capacity : 4096;
        if (capacity > writer->limit)
            capacity = writer->limit;
        while (capacity < required)
            capacity = capacity > writer->limit / 2 ? writer->limit : capacity * 2;
        void* grown = realloc(result->bytes, capacity);
        if (!grown) {
            snprintf(result->error, sizeof(result->error), "%s output allocation failed",
                     writer->codec);
            return 0;
        }
        result->bytes = (unsigned char*)grown;
        writer->capacity = capacity;
    }
    memcpy(result->bytes + result->size, bytes, length);
    result->size = required;
    return 1;
}
