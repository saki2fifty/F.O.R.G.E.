#pragma once
#include "texture_formats.hpp"
#include <TextureLoader.h>
#include <forge/texture_asset.hpp>
namespace forge::asset_detail {
TextureData copy_diligent_texture(Diligent::ITextureLoader& loader, TextureSemantic semantic,
                                  TextureAlpha alpha, TextureLimits limits = {});
} // namespace forge::asset_detail
