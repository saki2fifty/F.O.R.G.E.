#pragma once
#include "../audio_preview.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
namespace forge {
class AudioPreviewEditor {
  public:
    explicit AudioPreviewEditor(AudioOutput output = AudioOutput::Device) : output_(output) {}
    void stop() { preview_.stop(); }
    bool active() const { return preview_.active(); }
    void poll(const std::filesystem::path& project, const AssetRecord* record, bool permitted) {
        if (error_project_ != project || !record || error_asset_ != record->id)
            error_.clear();
        if (!permitted || !record || !preview_.matches(project, *record))
            stop();
        else
            preview_.poll();
    }
    void draw(const std::filesystem::path& project, const AssetRecord* record, bool locked) {
        poll(project, record, !locked);
        ui::heading("Clip preview", "Listen to this clip without entering game Play.");
        ImGui::BeginDisabled(locked || !record);
        if (ui::button(
                active() ? "Restart preview" : "Play preview",
                "Play the selected clip from the beginning. Preview changes are not saved.")) {
            try {
                preview_.configure(loop_, volume_);
                preview_.start(project, *record, output_);
                error_.clear();
            } catch (const std::exception& e) {
                error_ = e.what();
                error_project_ = project;
                error_asset_ = record->id;
            }
        }
        FORGE_UI_PROBE("audio-preview:play");
        ImGui::EndDisabled();
        ui::next_text_button("Stop preview");
        ImGui::BeginDisabled(!active());
        if (ui::button("Stop preview", "Stop and release this preview's sound and output device."))
            stop();
        FORGE_UI_PROBE("audio-preview:stop");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(locked || !record);
        const bool previous_loop = loop_;
        const float previous_volume = volume_;
        const bool loop_changed = ImGui::Checkbox("Loop preview", &loop_);
        FORGE_UI_PROBE("audio-preview:loop");
        ui::help("Repeat the preview until you stop it. This does not change Audio Source Loop.");
        const bool volume_changed = ImGui::SliderFloat("Preview volume", &volume_, 0, 1, "%.3f",
                                                       ImGuiSliderFlags_AlwaysClamp);
        FORGE_UI_PROBE("audio-preview:volume");
        ui::help("Temporary preview volume, independent of the game's saved mix volumes.");
        if (loop_changed || volume_changed) {
            try {
                preview_.configure(loop_, volume_);
            } catch (const std::exception& e) {
                loop_ = previous_loop;
                volume_ = previous_volume;
                error_ = e.what();
                error_project_ = project;
                error_asset_ = record->id;
            }
        }
        ImGui::EndDisabled();
        ImGui::TextUnformatted(active() ? "Preview playing" : "Preview stopped");
        if (locked)
            ImGui::TextWrapped("Stop game Play and finish saving import changes to preview.");
        if (!error_.empty())
            ImGui::TextWrapped("Preview unavailable: %s", error_.c_str());
    }
    void read_offline(std::span<float> output) { preview_.read_offline(output); }

  private:
    AudioClipPreview preview_;
    AudioOutput output_;
    bool loop_ = false;
    float volume_ = .5f;
    std::string error_;
    std::filesystem::path error_project_;
    AssetId error_asset_;
};
} // namespace forge
