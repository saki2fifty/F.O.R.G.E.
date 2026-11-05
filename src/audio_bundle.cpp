#include "audio_bundle.hpp"
#include "bounded_json.hpp"
#include <algorithm>
namespace forge::asset_detail {
namespace {
const ArtifactFile& file(std::span<const ArtifactFile> files, const char* name) {
    auto found =
        std::find_if(files.begin(), files.end(), [&](const auto& f) { return f.name == name; });
    if (found == files.end())
        throw std::runtime_error("Missing AudioClip bundle file");
    return *found;
}
} // namespace
AudioPlaybackStorage audio_playback_storage(std::string_view value) {
    if (value == "resident")
        return AudioPlaybackStorage::Resident;
    if (value == "stream")
        return AudioPlaybackStorage::Stream;
    throw std::runtime_error("Audio playback storage must be resident or stream");
}
nlohmann::json audio_clip_metadata(const AudioClipData& clip, std::string source_digest,
                                   std::string source_format, AudioPlaybackStorage storage) {
    validate_audio_clip(clip);
    return audio_clip_metadata(AudioClipInfo{clip.frames, clip.channels, clip.rate},
                               std::move(source_digest), std::move(source_format), storage);
}
nlohmann::json audio_clip_metadata(const AudioClipInfo& clip, std::string source_digest,
                                   std::string source_format, AudioPlaybackStorage storage) {
    validate_audio_info(clip);
    if (!valid_content_digest(source_digest))
        throw std::runtime_error("Invalid audio source digest");
    if (source_format != "wav" && source_format != "flac" && source_format != "mp3")
        throw std::runtime_error("Unsupported AudioClip source format");
    nlohmann::json result{{"format", "forge.audio-clip"},   {"version", 1},
                          {"source_format", source_format}, {"encoding", "pcm-f32-le"},
                          {"channels", clip.channels},      {"sample_rate", clip.rate},
                          {"frames", clip.frames},          {"duration", clip.duration()},
                          {"source_digest", source_digest}};
    if (storage == AudioPlaybackStorage::Stream) {
        if (clip.rate != audio_stream_sample_rate || clip.frames < audio_stream_sample_rate)
            throw std::runtime_error("Streamed audio requires at least one second of 48000 Hz PCM");
        result["playback_storage"] = "stream";
    } else if (storage != AudioPlaybackStorage::Resident)
        throw std::runtime_error("Unknown audio playback storage");
    return result;
}
nlohmann::json validate_audio_bundle(std::span<const ArtifactFile> files) {
    if (files.size() != 2)
        throw std::runtime_error("Unexpected AudioClip bundle file count");
    const auto clip = validate_audio_clip_bytes(file(files, "audio.fpcm").bytes);
    const auto metadata = parse_bounded_json(file(files, "audio.json").bytes, 16384, 128, 4);
    const auto expected = audio_clip_metadata(
        clip, metadata.at("source_digest").get<std::string>(),
        metadata.at("source_format").get<std::string>(),
        audio_playback_storage(metadata.value("playback_storage", std::string("resident"))));
    if (metadata != expected)
        throw std::runtime_error("AudioClip metadata does not match cooked PCM");
    return metadata;
}
AudioClipData audio_bundle_clip(std::span<const ArtifactFile> files) {
    (void)validate_audio_bundle(files);
    return decode_audio_clip(file(files, "audio.fpcm").bytes);
}
std::span<const std::byte> audio_bundle_clip_bytes(std::span<const ArtifactFile> files) {
    (void)validate_audio_bundle(files);
    return file(files, "audio.fpcm").bytes;
}
} // namespace forge::asset_detail
