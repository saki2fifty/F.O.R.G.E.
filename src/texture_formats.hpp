#pragma once
#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"
#include <forge/texture_asset.hpp>
namespace forge::asset_detail {
Diligent::TEXTURE_FORMAT diligent_texture_format(TextureFormat);
TextureFormat forge_texture_format(Diligent::TEXTURE_FORMAT);
} // namespace forge::asset_detail
