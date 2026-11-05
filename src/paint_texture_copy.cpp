#include "paint_texture_copy.hpp"
#include "model_render_resource.hpp"
#include "paint_texture_source.hpp"
#include "texture_image.hpp"
#include "texture_image_process.hpp"

namespace forge {
Rgba8Pixels load_paint_texture_pixels(const std::filesystem::path& project,
                                      const AssetCatalog& catalog, AssetRef<TextureAsset> asset,
                                      TextureSemantic semantic, const std::filesystem::path& worker,
                                      std::stop_token stop) {
    if (catalog.project_root() != std::filesystem::weakly_canonical(project))
        throw std::runtime_error("Paint pixel selection belongs to another project");
    if (semantic != TextureSemantic::Color && semantic != TextureSemantic::Data)
        throw std::runtime_error("Pixel painting supports Color or Data usage");
    TextureLimits limits;
    limits.dimension = paint_texture_dimension_limit;
    limits.bytes = 6ull * 1024 * 1024;
    auto selected =
        asset_detail::load_texture_selection(project, catalog, asset, semantic, limits, stop);
    const auto& texture = *selected.value;
    if (texture.dimension != TextureDimension::D2 || texture.layers != 1 || texture.depth != 1)
        throw std::runtime_error("Pixel copy requires a single 2D texture");
    if (semantic == TextureSemantic::Color && texture.alpha != TextureAlpha::Straight &&
        texture.alpha != TextureAlpha::Opaque)
        throw std::runtime_error("Color pixel copy requires straight or opaque alpha");
    const auto format = texture_format_info(texture.format);
    if (format.float_bits || format.signed_values ||
        (!format.compressed && format.block_bytes != format.channels) ||
        texture_hdr_block_storage(texture.format))
        throw std::runtime_error(
            "Pixel copy requires normalized byte or unsigned LDR block samples");
    if (format.compressed) {
        auto pixels = asset_detail::texture_compressed_pixels(worker, project, texture, {}, stop);
        if (auto* rgba = std::get_if<Rgba8Pixels>(&pixels))
            return std::move(*rgba);
        throw std::runtime_error("Pixel copy did not produce RGBA8 samples");
    }
    return texture_stored_pixels(texture, {}, stop);
}
} // namespace forge
