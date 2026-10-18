#include "view_image.hpp"
#include "asset_storage.hpp"
#include "texture_encode.h"
#include "view_image_limits.h"
#include <stdexcept>
namespace forge {
void save_view_png(const std::filesystem::path& path, unsigned width, unsigned height,
                   std::span<const std::uint8_t> rgba) {
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } encoded;
    if (!width || !height || width > FORGE_VIEW_IMAGE_MAX_EXTENT ||
        height > FORGE_VIEW_IMAGE_MAX_EXTENT || rgba.size() != std::size_t(width) * height * 4)
        throw std::runtime_error("View image requires complete RGBA8 display pixels");
    if (!forge_png_encode_rgba8(rgba.data(), rgba.size(), width, height, std::size_t(width) * 4,
                                FORGE_VIEW_IMAGE_ENCODED_LIMIT, &encoded))
        throw std::runtime_error(encoded.error);
    asset_storage::replace(
        path, std::string_view(reinterpret_cast<const char*>(encoded.bytes), encoded.size));
}
} // namespace forge
