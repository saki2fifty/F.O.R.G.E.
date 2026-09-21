#pragma once
#include "help.hpp"
#include <forge/assets.hpp>
namespace forge {
inline void draw_audio_details(const AssetRecord& record) {
    const auto found = record.metadata.find("forge.audio");
    if (found == record.metadata.end()) {
        ImGui::TextWrapped("Import this WAV to validate it and record its duration and format.");
        ui::help("Legacy registration preserves the clip identity. Importing it publishes "
                 "validated audio data without changing scene references.");
        return;
    }
    try {
        const auto& value = *found;
        ImGui::Text("Duration: %.3f s", value.at("duration").get<double>());
        ui::help("Duration of the successfully published clip, in seconds.");
        ImGui::Text("Channels: %u", value.at("channels").get<unsigned>());
        ui::help("One channel is mono; two channels are stereo. This importer preserves the source "
                 "channel count.");
        ImGui::Text("Sample rate: %u Hz", value.at("sample_rate").get<unsigned>());
        ui::help("Samples per second in each channel; the importer preserves the WAV rate.");
        ImGui::TextUnformatted("WAV source / 32-bit floating-point PCM");
        ui::help("The isolated importer decodes the WAV with miniaudio and publishes immutable "
                 "PCM. Restart Play to use a newer clip revision.");
    } catch (const std::exception&) {
        ImGui::TextWrapped("Audio metadata is unavailable or invalid. Reimport the source.");
        ui::help("The runtime independently checks the selected cooked bytes; invalid metadata "
                 "does not authorize playback.");
    }
}
} // namespace forge
