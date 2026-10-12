#pragma once
#include "../audio_mix.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <functional>
#include <optional>
namespace forge {
class AudioMixEditor {
  public:
    void reset() {
        draft_.reset();
        baseline_.reset();
        error_.clear();
    }
    bool dirty() const { return draft_ && baseline_ && *draft_ != *baseline_; }
    void draw(const std::string& session, const nlohmann::json& status, bool ready, bool pending,
              const std::string& error, const std::function<bool(const nlohmann::json&)>& submit) {
        if (session_ != session) {
            reset();
            session_ = session;
        }
        ui::heading("Live audio mix",
                    "Adjust the current Play session. Project defaults are unchanged.");
        ImGui::TextDisabled("PLAY — runtime only");
        if (!status.is_object() || !status.contains("master_volume") ||
            !status.contains("channel_volumes")) {
            reset();
            ImGui::TextWrapped(
                "Audio controls are available when Play reports an enabled audio output.");
            return;
        }
        AudioMixValues live;
        try {
            live = AudioMixValues::status(status);
        } catch (const std::exception& e) {
            reset();
            ImGui::TextWrapped("Audio status unavailable: %s", e.what());
            return;
        }
        if (!draft_ || (!pending && !dirty()))
            draft_ = baseline_ = live;
        ImGui::Text(
            "Output: %s | %s | %llu playing / %llu voices",
            status.value("output", "unknown").c_str(),
            status.value("paused", true) ? "paused" : "playing",
            static_cast<unsigned long long>(status.value("playing_sources", std::uint64_t{})),
            static_cast<unsigned long long>(status.value("voices", std::uint64_t{})));
        ImGui::Text("Clip cache: %llu clips | %.2f MiB",
                    static_cast<unsigned long long>(status.value("clips", std::uint64_t{})),
                    status.value("decoded_bytes", std::uint64_t{}) / (1024. * 1024.));
        ImGui::BeginDisabled(!ready || pending);
        const std::array labels{"Master", "Music", "Effects", "Dialogue"};
        for (unsigned i = 0; i < labels.size(); ++i) {
            float& volume = i == 0 ? draft_->master : draft_->channels[i - 1];
            float candidate = volume;
            if (ImGui::SliderFloat(labels[i], &candidate, 0, 1, "%.3f",
                                   ImGuiSliderFlags_AlwaysClamp)) {
                if (std::isfinite(candidate) && candidate >= 0 && candidate <= 1) {
                    volume = candidate;
                    error_.clear();
                } else
                    error_ = "Volume must be finite and between zero and one.";
            }
            FORGE_UI_PROBE(std::string("live-audio:") + labels[i]);
            ui::help("Linear volume. Zero silences this channel while playback time continues. "
                     "Master also affects sources routed directly to it. Apply sends this draft.");
        }
        ImGui::BeginDisabled(!dirty());
        if (ui::button("Apply live mix",
                       "Apply these four volumes to the running game. No project file changes.")) {
            try {
                if (submit(draft_->document())) {
                    baseline_ = draft_;
                    error_.clear();
                } else
                    error_ = "Runtime is busy. Apply again when its controls are ready.";
            } catch (const std::exception& e) {
                error_ = e.what();
            }
        }
        FORGE_UI_PROBE("live-audio:apply");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Reload live values",
                       "Discard this draft and use the runtime's current volumes."))
            draft_ = baseline_ = live;
        FORGE_UI_PROBE("live-audio:reload");
        ImGui::EndDisabled();
        if (pending)
            ImGui::TextUnformatted("Applying live mix...");
        else if (dirty())
            ImGui::TextUnformatted("Draft volumes — Apply to hear them.");
        if (!error.empty())
            ImGui::TextWrapped("Live audio mix rejected: %s", error.c_str());
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::Separator();
    }

  private:
    std::optional<AudioMixValues> draft_, baseline_;
    std::string session_, error_;
};
} // namespace forge
