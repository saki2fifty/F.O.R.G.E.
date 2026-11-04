#pragma once
#include "../model_render_resource.hpp"
#include "../texture_image.hpp"
#include "view_image.hpp"
#include <forge/texture_bundle.hpp>

namespace forge::ui {
// Compose the selected-artifact reader with the existing detached image writer.
// The copied catalog selects one immutable publication; no source decode occurs.
inline ViewImageTask::PixelInput texture_image_input(std::filesystem::path project,
                                                     std::shared_ptr<const AssetCatalog> catalog,
                                                     AssetRef<TextureAsset> texture,
                                                     std::optional<TextureSemantic> semantic = {},
                                                     TextureImageSlice slice = {}) {
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
                {"profile", "stored_rgba8"}};
    return {std::move(source),
            [project = std::move(project), catalog = std::move(catalog), texture, semantic, slice] {
                auto selected = asset_detail::load_texture_selection(
                    project, *catalog, texture, semantic, {texture_image_source_limit});
                return texture_stored_pixels(*selected.value, slice);
            }};
}
} // namespace forge::ui
