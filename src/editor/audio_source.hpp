#pragma once
#include "ui_probe.hpp"
#include "widgets.hpp"
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
              const std::function<bool(bool)>& submit) {
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
        if (available)
            ImGui::Text("%s | %.2f seconds | %s",
                        !status.value("playing", false) ? "Stopped"
                        : status.value("paused", true)  ? "Paused"
                                                        : "Playing",
                        status.value("duration", 0.),
                        status.value("loop", false) ? "Loop" : "Once");
        else
            ImGui::TextWrapped("%s",
                               status.is_object()
                                   ? status.value("error", "Waiting for source status...").c_str()
                                   : "Waiting for source status...");
        ImGui::BeginDisabled(!ready || pending || !available);
        auto button = [&](const char* label, const char* key, bool play) {
            if (ui::button(label,
                           play ? "Restart this runtime source from its beginning. Paused Play "
                                  "stays silent."
                                : "Stop this runtime source and reset it to the beginning.")) {
                if (submit(play))
                    error_.clear();
                else
                    error_ = "Play is busy. Try again when the controls are ready.";
            }
            FORGE_UI_PROBE(key);
        };
        button("Play / Restart", "live-audio-source:play", true);
        ImGui::SameLine();
        button("Stop sound", "live-audio-source:stop", false);
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
