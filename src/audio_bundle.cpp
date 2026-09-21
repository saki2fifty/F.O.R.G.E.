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
nlohmann::json audio_clip_metadata(const AudioClipData& clip, std::string source_digest) {
    validate_audio_clip(clip);
    if (!valid_content_digest(source_digest))
        throw std::runtime_error("Invalid WAV source digest");
    return {{"format", "forge.audio-clip"},  {"version", 1},
            {"source_format", "wav"},        {"encoding", "pcm-f32-le"},
            {"channels", clip.channels},     {"sample_rate", clip.rate},
            {"frames", clip.frames},         {"duration", clip.duration()},
            {"source_digest", source_digest}};
}
nlohmann::json validate_audio_bundle(std::span<const ArtifactFile> files) {
    if (files.size() != 2)
        throw std::runtime_error("Unexpected AudioClip bundle file count");
    const auto clip = decode_audio_clip(file(files, "audio.fpcm").bytes);
    const auto metadata = parse_bounded_json(file(files, "audio.json").bytes, 16384, 128, 4);
    const auto expected =
        audio_clip_metadata(clip, metadata.at("source_digest").get<std::string>());
    if (metadata != expected)
        throw std::runtime_error("AudioClip metadata does not match cooked PCM");
    return metadata;
}
AudioClipData audio_bundle_clip(std::span<const ArtifactFile> files) {
    (void)validate_audio_bundle(files);
    return decode_audio_clip(file(files, "audio.fpcm").bytes);
}
} // namespace forge::asset_detail
