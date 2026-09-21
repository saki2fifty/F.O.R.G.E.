#include "asset_bytes.hpp"
#include "audio_bundle.hpp"
#include "audio_importer.hpp"
namespace forge::asset_detail {
std::vector<ArtifactFile> execute_audio_recipe(const ImportProcessRequest& request,
                                               std::stop_token stop) {
    const auto& p = request.payload;
    if (stop.stop_requested())
        throw std::runtime_error("Audio import cancelled");
    if (p.at("recipe") != "forge.audio.wav" ||
        p.at("revision").get<std::string>() != audio_recipe_revision() ||
        request.inputs.size() != 1 || request.inputs.front().name != "source.wav")
        throw std::runtime_error("Invalid audio worker recipe/revision/input");
    (void)audio_settings().effective(p.at("settings").get<ImportSettingsDocument>());
    const auto& source = request.inputs.front().bytes;
    const auto digest = content_digest(source);
    if (digest != p.at("source_digest").get<std::string>())
        throw std::runtime_error("Audio source snapshot digest changed");
    const auto decoded = decode_audio_wav(source);
    if (stop.stop_requested())
        throw std::runtime_error("Audio import cancelled after decode");
    const auto text = audio_clip_metadata(decoded, digest).dump();
    const auto span = std::as_bytes(std::span(text));
    return {{"audio.fpcm", encode_audio_clip(decoded)}, {"audio.json", {span.begin(), span.end()}}};
}
} // namespace forge::asset_detail
