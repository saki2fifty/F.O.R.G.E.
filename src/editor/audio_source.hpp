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
                entity.at("components").contains("forge.audio_source"))
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
        const bool playing = available && status.value("playing", false);
        const bool source_paused = available && status.value("source_paused", false);
        if (available)
            ImGui::Text("%s | %.2f seconds | %s",
                        source_paused                  ? "Source paused"
                        : !playing                     ? "Stopped"
                        : status.value("paused", true) ? "Paused"
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
        ImGui::BeginDisabled(!ready || pending || (!available && !loading));
        auto button = [&](const char* label, const char* key, AudioSourceOperation operation) {
            if (ui::button(label,
                           operation == AudioSourceOperation::Restart
                               ? "Restart this runtime source from its beginning. Paused Play "
                                 "stays silent."
                           : operation == AudioSourceOperation::Stop
                               ? "Stop this runtime source and reset it to the beginning."
                               : "Pause or resume this source without restarting it. Game pause "
                                 "remains silent.")) {
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
        inline_if_fits(source_paused ? "Resume sound" : "Pause sound");
        ImGui::BeginDisabled(!playing && !source_paused);
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
