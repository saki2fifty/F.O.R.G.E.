#pragma once
#include "texture_image.hpp"

namespace forge::asset_detail {
// Borrowed selected plane after complete Texture validation. The caller retains
// the immutable TextureData owner; this never selects a different publication.
struct TextureImagePlane {
    TextureLayout layout;
    std::span<const std::byte> bytes;
};
TextureImagePlane texture_image_plane(const TextureData&, TextureImageSlice,
                                      std::size_t source_limit);
} // namespace forge::asset_detail
