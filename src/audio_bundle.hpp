#pragma once
#include "audio_asset.hpp"
#include <forge/derived_cache.hpp>
namespace forge::asset_detail {
// All callers validate both the PCM and metadata, including cache hits.
nlohmann::json audio_clip_metadata(const AudioClipData&, std::string source_digest);
nlohmann::json validate_audio_bundle(std::span<const ArtifactFile>);
AudioClipData audio_bundle_clip(std::span<const ArtifactFile>);
} // namespace forge::asset_detail
