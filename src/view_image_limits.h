#pragma once
// Shared private admission bounds for rendered RGBA8 images (C17 and C++).
#define FORGE_VIEW_IMAGE_MAX_EXTENT 4096u
#define FORGE_VIEW_IMAGE_PIXEL_LIMIT (64u * 1024u * 1024u)
#define FORGE_VIEW_IMAGE_ENCODED_LIMIT (68u * 1024u * 1024u)

// Detached linear RGB float storage at the same4096-axis limit (12bytes/pixel).
#define FORGE_HDR_IMAGE_PIXEL_LIMIT (192u * 1024u * 1024u)

// Precision-preserving normalized16-bit PNG profile; existing byte/HDR caps stay.
#define FORGE_UINT16_IMAGE_PIXEL_LIMIT (128u * 1024u * 1024u)
#define FORGE_UINT16_IMAGE_ENCODED_LIMIT (136u * 1024u * 1024u)

#define FORGE_JPEG_QUALITY_MIN 1
#define FORGE_JPEG_QUALITY_MAX 100
#define FORGE_JPEG_DEFAULT_QUALITY 90
