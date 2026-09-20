#pragma once
#include <TextureLoader.h>
#include <forge/texture_asset.hpp>
namespace forge::asset_detail {
Diligent::TEXTURE_FORMAT diligent_texture_format(TextureFormat format);
TextureFormat forge_texture_format(Diligent::TEXTURE_FORMAT format);
TextureData copy_diligent_texture(Diligent::ITextureLoader& loader, TextureSemantic semantic,
                                  TextureAlpha alpha, TextureLimits limits = {});
} // namespace forge::asset_detail
