#pragma once
#include "audio_asset.hpp"
#include "import_process.hpp"
#include <array>
#include <forge/asset_importer.hpp>
namespace forge::asset_detail {
struct AudioSourceProfile {
    std::string_view importer, extension, label;
    AudioSourceFormat format;
};
inline constexpr std::array audio_source_profiles{
    AudioSourceProfile{"forge.audio.wav", ".wav", "WAV", AudioSourceFormat::Wav},
    AudioSourceProfile{"forge.audio.flac", ".flac", "FLAC", AudioSourceFormat::Flac},
    AudioSourceProfile{"forge.audio.mp3", ".mp3", "MP3", AudioSourceFormat::Mp3}};
inline constexpr const AudioSourceProfile* audio_source_profile(std::string_view importer) {
    for (const auto& profile : audio_source_profiles)
        if (profile.importer == importer)
            return &profile;
    return nullptr;
}
std::shared_ptr<const AssetImporter>
audio_importer(std::filesystem::path worker, const AudioSourceProfile& = audio_source_profiles[0]);
ImportSettingsSchema audio_settings(std::string_view importer = "forge.audio.wav");
std::string audio_recipe_revision();
WorkerLimits audio_worker_limits();
std::vector<ArtifactFile> execute_audio_recipe(const ImportProcessRequest&, std::stop_token = {});
} // namespace forge::asset_detail
