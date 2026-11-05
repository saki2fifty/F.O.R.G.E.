#pragma once
#include "audio_asset.hpp"
#include <forge/derived_cache.hpp>
namespace forge::asset_detail {
enum class AudioPlaybackStorage { Resident, Stream };
AudioPlaybackStorage audio_playback_storage(std::string_view);
// All callers validate both the PCM and metadata, including cache hits.
nlohmann::json audio_clip_metadata(const AudioClipData&, std::string source_digest,
                                   std::string source_format = "wav",
                                   AudioPlaybackStorage = AudioPlaybackStorage::Resident);
nlohmann::json audio_clip_metadata(const AudioClipInfo&, std::string source_digest,
                                   std::string source_format,
                                   AudioPlaybackStorage = AudioPlaybackStorage::Resident);
nlohmann::json validate_audio_bundle(std::span<const ArtifactFile>);
AudioClipData audio_bundle_clip(std::span<const ArtifactFile>);
// Validated private PCM transport, including its header. The returned view is
// borrowed only while the supplied owned artifact remains alive.
std::span<const std::byte> audio_bundle_clip_bytes(std::span<const ArtifactFile>);
} // namespace forge::asset_detail
