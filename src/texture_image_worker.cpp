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
        payload.at("recipe") != nlohmann::json(texture_image_decode_recipe) ||
        (payload.at("profile") != nlohmann::json(texture_image_decode_profile) &&
         payload.at("profile") != nlohmann::json(texture_signed_image_decode_profile) &&
         payload.at("profile") != nlohmann::json(texture_hdr_image_decode_profile)) ||
        request.inputs.size() != 1 || request.inputs[0].name != "blocks.bin")
        throw std::runtime_error("Invalid compressed Texture image worker request");
    auto number = [&](const char* key, unsigned limit) {
        const auto& value = payload.at(key);
        if (!value.is_number_unsigned() || value.get<std::uint64_t>() > limit)
            throw std::runtime_error("Invalid compressed Texture image worker integer");
        return value.get<unsigned>();
    };
    const auto format = TextureFormat(number("format", unsigned(TextureFormat::BC7Srgb)));
    const bool signed_samples =
        payload.at("profile") == nlohmann::json(texture_signed_image_decode_profile);
    const bool hdr = payload.at("profile") == nlohmann::json(texture_hdr_image_decode_profile);
    if (signed_samples != texture_snorm_block_storage(format) ||
        hdr != texture_hdr_block_storage(format))
        throw std::runtime_error("Compressed Texture image profile differs from its storage");
    if (hdr) {
        std::vector<ArtifactFile> output;
        output.push_back(
            {"rgba16f-le.bin",
             decode_texture_hdr_block_image(format, number("width", FORGE_VIEW_IMAGE_MAX_EXTENT),
                                            number("height", FORGE_VIEW_IMAGE_MAX_EXTENT),
                                            request.inputs[0].bytes, stop)});
        return output;
    }
    if (signed_samples) {
        auto pixels = decode_texture_signed_block_image(
            format, number("width", FORGE_VIEW_IMAGE_MAX_EXTENT),
            number("height", FORGE_VIEW_IMAGE_MAX_EXTENT), request.inputs[0].bytes, stop);
        std::vector<std::byte> bytes(pixels.rgba.size() * 2);
        for (std::size_t i = 0; i < pixels.rgba.size(); ++i) {
            bytes[i * 2] = std::byte(pixels.rgba[i] & 255);
            bytes[i * 2 + 1] = std::byte(pixels.rgba[i] >> 8);
        }
        return {{"rgba16le.bin", std::move(bytes)}};
    }
    auto pixels = decode_texture_block_image(format, number("width", FORGE_VIEW_IMAGE_MAX_EXTENT),
                                             number("height", FORGE_VIEW_IMAGE_MAX_EXTENT),
                                             request.inputs[0].bytes, stop);
    std::vector<std::byte> bytes(pixels.rgba.size());
    std::memcpy(bytes.data(), pixels.rgba.data(), bytes.size());
    return {{"rgba8.bin", std::move(bytes)}};
}
} // namespace forge::asset_detail
