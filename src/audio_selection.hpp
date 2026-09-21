#pragma once
#include "audio_asset.hpp"
#include <forge/assets.hpp>
namespace forge::asset_detail {
AudioClipData load_audio_clip(const std::filesystem::path& project, const AssetRecord&);
} // namespace forge::asset_detail
