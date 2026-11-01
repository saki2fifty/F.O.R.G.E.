#pragma once
#include "sprite_atlas_packing.hpp"
#include <nlohmann/json.hpp>
namespace forge::asset_detail {
nlohmann::json sprite_atlas_settings(const SpriteAtlasPacking&);
SpriteAtlasPacking sprite_atlas_settings(const nlohmann::json&);
} // namespace forge::asset_detail
