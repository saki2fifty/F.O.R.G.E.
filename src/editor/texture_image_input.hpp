#pragma once
#include "../model_render_resource.hpp"
#include "../texture_image.hpp"
#include "../texture_image_process.hpp"
#include "view_image.hpp"
#include <forge/texture_bundle.hpp>

namespace forge::ui {
// Compose the selected-artifact reader with the existing detached image writer.
// The copied catalog selects one immutable publication; no source decode occurs.
inline ViewImageTask::PixelInput
texture_image_input(std::filesystem::path project, std::shared_ptr<const AssetCatalog> catalog,
                    AssetRef<TextureAsset> texture, std::optional<TextureSemantic> semantic = {},
                    TextureImageSlice slice = {}, std::optional<ViewImageFormat> only_format = {},
                    std::filesystem::path worker = {}) {
    if (!catalog)
        throw std::runtime_error("Texture image export needs a catalog selection");
    if (catalog->project_root() != std::filesystem::weakly_canonical(project))
        throw std::runtime_error("Texture selection belongs to another project");
    const auto found = catalog->records().find(texture.id);
    if (found == catalog->records().end() || found->second.type != TextureAsset::type)
        throw std::runtime_error("Choose a published Texture asset");
    if (!found->second.metadata.contains("forge.import"))
        throw std::runtime_error("Publish a cooked Texture before exporting stored pixels");
    Json source{{"asset", texture.id},
                {"semantic", semantic ? std::string(texture_variant_key(*semantic)) : "automatic"},
                {"mip", slice.mip},
                {"layer", slice.layer},
                {"face", slice.face},
                {"depth", slice.depth},
                {"profile", "stored_samples"}};
    return {std::move(source),
            [project = std::move(project), catalog = std::move(catalog), texture, semantic, slice,
             worker = std::move(worker)](ViewImageFormat format) -> ImagePixels {
                const bool hdr = format == ViewImageFormat::Hdr;
                auto selected = asset_detail::load_texture_selection(
                    project, *catalog, texture, semantic,
                    {hdr                              ? texture_hdr_source_limit
                     : format == ViewImageFormat::Png ? texture_uint16_source_limit
                                                      : texture_image_source_limit});
                if (hdr)
                    return texture_hdr_pixels(*selected.value, slice);
                if (texture_format_info(selected.value->format).compressed)
                    return asset_detail::texture_compressed_pixels(worker, project, *selected.value,
                                                                   slice);
                if (texture_uint16_storage(selected.value->format)) {
                    if (format != ViewImageFormat::Png)
                        throw std::runtime_error(
                            "Normalized16-bit samples require PNG (.png) output");
                    return texture_stored_uint16_pixels(*selected.value, slice);
                }
                return texture_stored_pixels(*selected.value, slice);
            },
            true, only_format};
}
} // namespace forge::ui
