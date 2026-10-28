#include "sprite_atlas_source.hpp"
#include "sprite_material_profile.hpp"
#include "texture_encode.h"
#include "view_image_limits.h"
#include <algorithm>

namespace forge::asset_detail {
std::vector<ArtifactFile> execute_sprite_atlas_source(const ImportProcessRequest& request,
                                                      std::stop_token stop) {
    const auto& payload = request.payload;
    if (!payload.is_object() || payload.size() != 3 ||
        payload.at("recipe") != sprite_atlas_source_recipe ||
        payload.at("revision") != sprite_atlas_source_revision())
        throw std::runtime_error("Atlas worker/toolchain revision mismatch");
    const auto settings = sprite_atlas_settings(payload.at("settings"));
    if (request.inputs.empty() || request.inputs.size() > sprite_frame_region_limit)
        throw std::runtime_error("Atlas worker needs between1 and512 image snapshots");
    std::vector<TextureData> images;
    std::vector<const TextureData*> borrowed;
    images.reserve(request.inputs.size());
    auto remaining = settings.byte_limit;
    for (std::size_t index = 0; index < request.inputs.size(); ++index) {
        if (stop.stop_requested())
            throw std::runtime_error("Atlas source generation cancelled");
        const auto& input = request.inputs[index];
        if (input.name != sprite_atlas_input_name(index))
            throw std::runtime_error("Atlas image snapshot order/name changed");
        TextureLimits limits;
        limits.bytes = remaining;
        limits.dimension = settings.max_size;
        auto texture = decode_texture(input.bytes, limits);
        validate_sprite_texture_profile(texture);
        if (texture.format != TextureFormat::RGBA8Srgb || texture.mips != 1)
            throw std::runtime_error("Atlas PNG source requires sRGB RGBA8 base-level images");
        remaining -= texture.byte_size();
        images.push_back(std::move(texture));
    }
    for (const auto& image : images)
        borrowed.push_back(&image);
    auto result = build_sprite_atlas(borrowed, settings, stop);
    images.clear();
    borrowed.clear(); // Release input pixels before native encoding.
    if (stop.stop_requested())
        throw std::runtime_error("Atlas source generation cancelled");
    struct Encoded : ForgeImageBytes {
        Encoded() : ForgeImageBytes{} {}
        ~Encoded() { forge_image_bytes_free(this); }
    } png;
    const auto& texture = result.texture;
    const auto& pixels = texture.subresources.front();
    if (!forge_png_encode_rgba8(pixels.data(), pixels.size(), texture.width, texture.height,
                                std::size_t(texture.width) * 4, FORGE_VIEW_IMAGE_ENCODED_LIMIT,
                                &png))
        throw std::runtime_error(png.error);
    if (stop.stop_requested())
        throw std::runtime_error("Atlas source generation cancelled");
    const auto layout = sprite_atlas_layout_document(result.layout).dump();
    const auto* first = reinterpret_cast<const std::byte*>(png.bytes);
    return {{"atlas.png", {first, first + png.size}},
            {"layout.json",
             {reinterpret_cast<const std::byte*>(layout.data()),
              reinterpret_cast<const std::byte*>(layout.data() + layout.size())}}};
}
} // namespace forge::asset_detail
