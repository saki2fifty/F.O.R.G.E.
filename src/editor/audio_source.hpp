#pragma once
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <forge/audio_service.hpp>
#include <forge/identity.hpp>
#include <functional>
#include <optional>
namespace forge {
class AudioSourceEditor {
  public:
    static std::optional<EntityRef> selected(const nlohmann::json& effective,
                                             const std::string& selected) {
        if (!effective.is_object() || !effective.contains("asset_id") ||
            !effective.contains("entities"))
            return {};
        for (const auto& entity : effective.at("entities"))
            if (entity.value("id", "") == selected && entity.contains("components") &&
                (entity.at("components").contains("forge.audio_source") ||
                 entity.at("components").contains("forge.sound_cue")))
                return EntityRef{effective.at("asset_id").get<AssetId>(),
                                 EntityId::parse(selected)};
        return {};
    }
    void draw(const std::string& session, EntityRef source, const nlohmann::json& status,
              bool ready, bool pending, const std::string& error,
              const std::function<bool(AudioSourceOperation)>& submit) {
        if (session_ != session || source_ != source) {
            error_.clear();
            session_ = session;
            source_ = source;
        }
        ImGui::PushID("live-audio-source");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.075f, 0.18f, 0.20f, 1});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.24f, 0.67f, 0.67f, 1});
        ImGui::BeginChild("##live-audio-source", {0, 0},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ui::heading("Live sound", "Control this source in the current Play session.");
        ImGui::TextColored({.61f, .93f, .89f, 1}, "PLAY — runtime only");
        const bool available = status.is_object() && status.value("available", false);
        const bool loading = status.is_object() && status.value("pending", false);
        const bool admitted = available || loading;
        if (status.is_object() && status.value("kind", std::string{}) == "sound_cue") {
            ImGui::Text("Accepted triggers: %llu", static_cast<unsigned long long>(status.value("accepted_triggers",std::uint64_t{})));
            ImGui::Text("Maximum one-shots per trigger: %u / 16", status.value("maximum_outputs",0u));
            ui::help("Each trigger composes existing Audio Source one-shots. Individual sources own playback, pause and stop. Choices reset when the graph or runtime world is replaced.");
            ImGui::BeginDisabled(!ready || pending || !available);
            if (ui::button("Trigger cue", "Queue one event through the existing audio owner.")) {
                error_ = submit(AudioSourceOperation::OneShot) ? "" : "Play is busy. Try again when controls are ready.";
            }
            FORGE_UI_PROBE("live-sound-cue:trigger");
            ImGui::EndDisabled();
            if (!error.empty()) ImGui::TextWrapped("Cue rejected: %s",error.c_str());
            if (!error_.empty()) ImGui::TextWrapped("%s",error_.c_str());
            ImGui::EndChild(); ImGui::PopStyleColor(2); ImGui::PopID();
            return;
        }
        const bool effect_tail =
            admitted && status.value("effects_tails", status.value("echo_tails", 0u)) > 0;
        const bool playing = admitted && (status.value("playing", false) ||
                                          status.value("playing_one_shots", 0u) > 0 || effect_tail);
        const bool source_paused = admitted && (status.value("source_paused", false) ||
                                                status.value("paused_one_shots", 0u) > 0);
        const bool delayed = admitted && status.value("delayed_one_shots", 0u) > 0;
        if (available)
            ImGui::Text("%s | %.2f seconds | %s",
                        source_paused                  ? "Source paused"
                        : !playing && !delayed          ? "Stopped"
                        : status.value("paused", true) ? "Paused"
                        : delayed && !playing           ? "Waiting to start"
                        : effect_tail                  ? "Effect tail"
                                                       : "Playing",
                        status.value("duration", 0.),
                        status.value("loop", false) ? "Loop" : "Once");
        else if (loading)
            ImGui::TextUnformatted("Loading sound...");
        else
            ImGui::TextWrapped("%s",
                               status.is_object()
                                   ? status.value("error", "Waiting for source status...").c_str()
                                   : "Waiting for source status...");
        if (admitted)
            ImGui::Text("Overlapping effects: %u / 16", status.value("one_shots", 0u));
        if (delayed)
            ImGui::Text("Delayed one-shots: %u", status.value("delayed_one_shots", 0u));
        ImGui::BeginDisabled(!ready || pending || (!available && !loading));
        auto button = [&](const char* label, [[maybe_unused]] const char* key,
                          AudioSourceOperation operation) {
            if (ui::button(label,
                           operation == AudioSourceOperation::Restart
                               ? "Restart this runtime source from its beginning. Pause with game "
                                 "controls whether gameplay pause silences it."
                           : operation == AudioSourceOperation::Stop
                               ? "Stop this source and all its overlapping effects."
                           : operation == AudioSourceOperation::OneShot
                               ? "Play the selected clip once alongside existing sound. The main "
                                 "sound keeps its position. Pause with game controls whether "
                                 "gameplay pause silences it."
                               : "Pause or resume this source and its effects without restarting. "
                                 "Playback follows this source's Pause with game setting.")) {
                if (submit(operation))
                    error_.clear();
                else
                    error_ = "Play is busy. Try again when the controls are ready.";
            }
            FORGE_UI_PROBE(key);
        };
        auto inline_if_fits = [&](const char* label) {
            const auto& style = ImGui::GetStyle();
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            const float width = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;
            if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
                ImGui::SameLine();
        };
        button("Play / Restart", "live-audio-source:play", AudioSourceOperation::Restart);
        inline_if_fits("Play one-shot");
        button("Play one-shot", "live-audio-source:one-shot", AudioSourceOperation::OneShot);
        inline_if_fits(source_paused ? "Resume sound" : "Pause sound");
        ImGui::BeginDisabled(!playing && !delayed && !source_paused);
        button(source_paused ? "Resume sound" : "Pause sound", "live-audio-source:pause",
               source_paused ? AudioSourceOperation::Resume : AudioSourceOperation::Pause);
        ImGui::EndDisabled();
        inline_if_fits("Stop sound");
        button("Stop sound", "live-audio-source:stop", AudioSourceOperation::Stop);
        ImGui::EndDisabled();
        if (pending)
            ImGui::TextUnformatted("Applying source playback...");
        if (!error.empty())
            ImGui::TextWrapped("Source playback rejected: %s", error.c_str());
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

  private:
    EntityRef source_{};
    std::string session_, error_;
};
} // namespace forge
