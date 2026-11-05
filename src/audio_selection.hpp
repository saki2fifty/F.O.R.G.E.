#pragma once
#include "audio_asset.hpp"
#include <forge/assets.hpp>
#include <memory>
namespace forge::asset_detail {
class AudioClipResource;
struct AudioStreamBudget;
AudioClipData load_audio_clip(const std::filesystem::path& project, const AssetRecord&);
std::unique_ptr<AudioClipResource> load_audio_resource(const std::filesystem::path& project,
                                                       const AssetRecord&,
                                                       std::shared_ptr<AudioStreamBudget>,
                                                       std::stop_token = {});
} // namespace forge::asset_detail
