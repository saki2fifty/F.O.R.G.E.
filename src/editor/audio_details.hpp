#pragma once
#include "help.hpp"
#include <forge/assets.hpp>
namespace forge {
inline void draw_audio_details(const AssetRecord& record) {
    const auto found = record.metadata.find("forge.audio");
    if (found == record.metadata.end()) {
        ImGui::TextWrapped(
            "Import this audio source to validate it and record its duration and format.");
        ui::help("Legacy registration preserves the clip identity. Importing it publishes "
                 "validated audio data without changing scene references.");
        return;
    }
    try {
        const auto& value = *found;
        ImGui::Text("Duration: %.3f s", value.at("duration").get<double>());
        ui::help("Duration of the successfully published clip, in seconds.");
        ImGui::Text("Channels: %u", value.at("channels").get<unsigned>());
        ui::help("One channel is mono; two channels are stereo. Force mono combines stereo "
                 "channels during import.");
        ImGui::Text("Sample rate: %u Hz", value.at("sample_rate").get<unsigned>());
        ui::help("Samples per second in each cooked channel. Streamed clips use 48000 Hz; "
                 "resident clips preserve the source rate.");
        const bool stream = value.value("playback_storage", std::string("resident")) == "stream";
        ImGui::Text("Playback storage: %s", stream ? "Stream" : "Resident");
        ui::help("Resident keeps the clip's decoded PCM in memory. Stream prepares an owned "
                 "temporary copy and reads bounded pages on worker threads. Stream requires "
                 "at least one second and supports eight simultaneous voices.");
        const auto format = value.at("source_format").get<std::string>();
        const char* label = format == "wav"    ? "WAV"
                            : format == "flac" ? "FLAC"
                            : format == "mp3"  ? "MP3"
                                               : nullptr;
        if (!label)
            throw std::runtime_error("Unsupported AudioClip source format");
        ImGui::Text("%s source / 32-bit floating-point PCM", label);
        ui::help(
            "The isolated importer decodes the recording with miniaudio and publishes immutable "
            "PCM. Restart Play to use a newer clip revision.");
    } catch (const std::exception&) {
        ImGui::TextWrapped("Audio metadata is unavailable or invalid. Reimport the source.");
        ui::help("The runtime independently checks the selected cooked bytes; invalid metadata "
                 "does not authorize playback.");
    }
}
} // namespace forge
