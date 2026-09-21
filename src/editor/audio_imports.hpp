#pragma once
#include "../audio_authoring.hpp"
#include "../texture_authoring.hpp"
#include "asset_import_editor.hpp"
#include "audio_details.hpp"
namespace forge {
class AudioImportEditor : public AssetImportEditor {
  public:
    explicit AudioImportEditor(std::filesystem::path worker)
        : AssetImportEditor({"audio_import", "Audio clip", "Assets/sound.wav",
                             "A mono/stereo WAV inside this project, up to 16 MiB. Validation runs "
                             "in an isolated worker.",
                             "Import validates the clip, records its duration and preserves its "
                             "AssetId. Restart Play after publishing a new revision.",
                             "Import audio...", desktop_texture_target(),
                             [worker = std::move(worker)] { return audio_import_registry(worker); },
                             [](auto& candidate, const auto& plan, const auto&, auto decisions) {
                                 if (!decisions.empty())
                                     throw std::runtime_error(
                                         "AudioClip has no subasset correspondence decisions");
                                 prepare_audio_publication(candidate, plan);
                             }}) {}
};
} // namespace forge
