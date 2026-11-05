#pragma once
#include "image_pixels.hpp"
#include <forge/assets.hpp>
#include <forge/texture_asset.hpp>
#include <stop_token>

namespace forge {
// A copied immutable publication, not a source-image decoder or asset mutation.
// Only the document's matching Color/Data usage is selected. Unsupported sample
// precision/alpha profiles reject instead of silently changing interpretation.
Rgba8Pixels load_paint_texture_pixels(const std::filesystem::path& project, const AssetCatalog&,
                                      AssetRef<TextureAsset>, TextureSemantic,
                                      const std::filesystem::path& worker, std::stop_token = {});
} // namespace forge
