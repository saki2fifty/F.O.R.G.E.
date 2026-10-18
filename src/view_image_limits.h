#pragma once
// Shared private admission bounds for rendered RGBA8 images (C17 and C++).
#define FORGE_VIEW_IMAGE_MAX_EXTENT 4096u
#define FORGE_VIEW_IMAGE_PIXEL_LIMIT (64u * 1024u * 1024u)
#define FORGE_VIEW_IMAGE_ENCODED_LIMIT (68u * 1024u * 1024u)
