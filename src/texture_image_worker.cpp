#include "texture_block_image.hpp"
#include "texture_image_process.hpp"
#include "view_image_limits.h"
#include <cstring>
#include <stdexcept>

namespace forge::asset_detail {
std::vector<ArtifactFile> execute_texture_image_recipe(const ImportProcessRequest& request,
                                                       std::stop_token stop) {
    const auto& payload = request.payload;
    if (!payload.is_object() || payload.size() != 5 ||
        payload.at("recipe") != texture_image_decode_recipe ||
        payload.at("profile") != texture_image_decode_profile || request.inputs.size() != 1 ||
        request.inputs[0].name != "blocks.bin")
        throw std::runtime_error("Invalid compressed Texture image worker request");
    auto number = [&](const char* key, unsigned limit) {
        const auto& value = payload.at(key);
        if (!value.is_number_unsigned() || value.get<std::uint64_t>() > limit)
            throw std::runtime_error("Invalid compressed Texture image worker integer");
        return value.get<unsigned>();
    };
    const auto format = TextureFormat(number("format", unsigned(TextureFormat::BC7Srgb)));
    auto pixels = decode_texture_block_image(format, number("width", FORGE_VIEW_IMAGE_MAX_EXTENT),
                                             number("height", FORGE_VIEW_IMAGE_MAX_EXTENT),
                                             request.inputs[0].bytes, stop);
    std::vector<std::byte> bytes(pixels.rgba.size());
    std::memcpy(bytes.data(), pixels.rgba.data(), bytes.size());
    return {{"rgba8.bin", std::move(bytes)}};
}
} // namespace forge::asset_detail
