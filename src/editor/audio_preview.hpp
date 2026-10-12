#pragma once
#include "../audio_preview.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <algorithm>
#include <cmath>
namespace forge {
class AudioPreviewEditor {
  public:
    explicit AudioPreviewEditor(AudioOutput output = AudioOutput::Device) : output_(output) {}
    void stop() { preview_.clear(); }
    bool active() const { return preview_.active(); }
    bool paused() const { return preview_.paused(); }
    double position() const { return preview_.position(); }
    void poll(const std::filesystem::path& project, const AssetRecord* record, bool permitted) {
        if (error_project_ != project || !record || error_asset_ != record->id)
            error_.clear();
        if (!permitted || !record || (preview_.active() && !preview_.matches(project, *record)) ||
            (preview_.waveform() && !preview_.waveform_matches(project, *record)))
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
            preview_.stop();
        FORGE_UI_PROBE("audio-preview:stop");
        ImGui::EndDisabled();
        ui::next_text_button(preview_.paused() ? "Resume preview" : "Pause preview");
        ImGui::BeginDisabled(!active());
        if (ui::button(preview_.paused() ? "Resume preview" : "Pause preview",
                       "Pause or resume this preview without changing its playback position."))
            attempt(project, record, [&] { preview_.paused(!preview_.paused()); });
        FORGE_UI_PROBE("audio-preview:pause");
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
        ImGui::BeginDisabled(locked || !record || preview_.waveform());
        if (ui::button("Show waveform",
                       "Load a bounded waveform envelope without opening a sound device."))
            attempt(project, record, [&] { preview_.load_waveform(project, *record); });
        FORGE_UI_PROBE("audio-preview:waveform-load");
        ImGui::EndDisabled();
        if (const auto* envelope = preview_.waveform()) {
            const double duration = envelope->duration();
            if (!ImGui::IsAnyItemActive())
                position_ = preview_.position();
            ImGui::Text("%.3f / %.3f seconds | %u Hz | %u channel%s", preview_.position(), duration,
                        envelope->rate, envelope->channels, envelope->channels == 1 ? "" : "s");
            ImGui::BeginDisabled(!active());
            const double zero = 0;
            ImGui::SliderScalar("Position (seconds)", ImGuiDataType_Double, &position_, &zero,
                                &duration, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            FORGE_UI_PROBE("audio-preview:position");
            if (ImGui::IsItemDeactivatedAfterEdit())
                attempt(project, record, [&] { preview_.seek(position_); });
            ImGui::EndDisabled();
            waveform(*envelope, preview_.position(), project, record);
        }
        ImGui::TextUnformatted(!active()           ? "Preview stopped"
                               : preview_.paused() ? "Preview paused"
                                                   : "Preview playing");
        if (locked)
            ImGui::TextWrapped("Stop game Play and finish saving import changes to preview.");
        if (!error_.empty())
            ImGui::TextWrapped("Preview unavailable: %s", error_.c_str());
    }
    void read_offline(std::span<float> output) { preview_.read_offline(output); }

  private:
    template <class F>
    void attempt(const std::filesystem::path& project, const AssetRecord* record, F action) {
        try {
            action();
            error_.clear();
        } catch (const std::exception& e) {
            error_ = e.what();
            error_project_ = project;
            error_asset_ = record ? record->id : AssetId{};
        }
    }
    void waveform(const asset_detail::AudioWaveform& envelope, double position,
                  const std::filesystem::path& project, const AssetRecord* record) {
        const float width = std::max(1.f, ImGui::GetContentRegionAvail().x);
        const float band = 70.f * ui::interface_scale;
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Waveform##audio-preview", {width, band * envelope.channels});
        FORGE_UI_PROBE("audio-preview:waveform");
        const bool clicked = ImGui::IsItemClicked();
        ui::help("Click the waveform during preview to seek. Each band shows one channel; this is "
                 "not a waveform editor.");
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 end{origin.x + width, origin.y + band * envelope.channels};
        draw->PushClipRect(origin, end, true);
        draw->AddRectFilled(origin, end, ImGui::GetColorU32(ImGuiCol_FrameBg));
        float magnitude = 1;
        for (const auto& peak : envelope.peaks)
            magnitude = std::max(magnitude, std::max(std::abs(peak[0]), std::abs(peak[1])));
        for (unsigned channel = 0; channel < envelope.channels; ++channel) {
            const float center = origin.y + band * (channel + .5f);
            draw->AddLine({origin.x, center}, {end.x, center}, ImGui::GetColorU32(ImGuiCol_Border));
            const auto columns =
                std::min(envelope.buckets(), std::size_t(std::max(1.f, std::floor(width))));
            for (std::size_t i = 0; i < columns; ++i) {
                const auto begin = i * envelope.buckets() / columns;
                const auto finish = (i + 1) * envelope.buckets() / columns;
                auto peak = envelope.peaks[begin * envelope.channels + channel];
                for (auto bucket = begin + 1; bucket < finish; ++bucket) {
                    const auto& next = envelope.peaks[bucket * envelope.channels + channel];
                    peak[0] = std::min(peak[0], next[0]);
                    peak[1] = std::max(peak[1], next[1]);
                }
                const float x = origin.x + width * (float(i) + .5f) / columns;
                draw->AddLine({x, center - peak[1] / magnitude * band * .45f},
                              {x, center - peak[0] / magnitude * band * .45f},
                              ImGui::GetColorU32(ImGuiCol_PlotHistogram));
            }
            draw->AddText({origin.x + 5, origin.y + band * channel + 3},
                          ImGui::GetColorU32(ImGuiCol_Text),
                          envelope.channels == 1 ? "Mono"
                          : channel == 0         ? "Left"
                                                 : "Right");
        }
        const float cursor = origin.x + width * float(position / envelope.duration());
        draw->AddLine({cursor, origin.y}, {cursor, end.y}, ImGui::GetColorU32(ImGuiCol_PlotLines),
                      2);
        draw->PopClipRect();
        if (clicked && active()) {
            const double fraction =
                std::clamp(double((ImGui::GetIO().MousePos.x - origin.x) / width), 0., 1.);
            attempt(project, record, [&] { preview_.seek(fraction * envelope.duration()); });
        }
    }
    AudioClipPreview preview_;
    AudioOutput output_;
    bool loop_ = false;
    float volume_ = .5f;
    double position_ = 0;
    std::string error_;
    std::filesystem::path error_project_;
    AssetId error_asset_;
};
} // namespace forge
