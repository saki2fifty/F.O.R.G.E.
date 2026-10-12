#pragma once
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <forge/identity.hpp>
#include <functional>
#include <optional>
namespace forge {
class AnimationPlaybackEditor {
  public:
    static std::optional<EntityRef> selected(const nlohmann::json& effective,
                                             const std::string& selected) {
        if (!effective.is_object() || !effective.contains("asset_id") ||
            !effective.contains("entities"))
            return {};
        for (const auto& row : effective.at("entities"))
            if (row.value("id", "") == selected && row.contains("components") &&
                row.at("components").contains("forge.animator"))
                return EntityRef{effective.at("asset_id").get<AssetId>(),
                                 EntityId::parse(selected)};
        return {};
    }
    void draw(const std::string& session, EntityRef ref, const nlohmann::json& status, bool ready,
              bool pending, const std::string& error,
              const std::function<bool(const nlohmann::json&)>& submit) {
        if (session_ != session || source_ != ref) {
            session_ = session;
            source_ = ref;
            dirty_ = false;
            error_.clear();
        }
        const bool available = status.is_object() && status.value("available", false);
        const bool playing = available && status.value("playing", false);
        const double duration = available ? status.value("duration", 0.) : 0.;
        if (!dirty_)
            position_ = available ? status.value("time", 0.) : 0.;
        ImGui::PushID("live-animation");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{.075f, .18f, .20f, 1});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{.24f, .67f, .67f, 1});
        ImGui::BeginChild("##live-animation", {0, 0},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ui::heading("Live animation",
                    "Adjust this entity's animation in the current Play session.");
        ImGui::TextColored({.61f, .93f, .89f, 1}, "PLAY — runtime only");
        if (available) {
            ImGui::TextWrapped("%s | %.3f / %.3f seconds",
                               !status.value("enabled", true)    ? "Disabled"
                               : status.value("finished", false) ? "Finished"
                               : playing                         ? "Playing"
                                                                 : "Clip paused",
                               status.value("time", 0.), duration);
            const auto state = status.value("state_name", std::string());
            if (!state.empty())
                ImGui::TextWrapped("State: %s", state.c_str());
            ImGui::TextWrapped("%s | Speed %.3f", status.value("loop", false) ? "Loop" : "One-shot",
                               status.value("speed", 0.));
            if (status.value("transitioning", false))
                ImGui::TextWrapped("Incoming fade: %.1f%%",
                                   status.value("transition_weight", 1.) * 100);
            if (status.value("blend_weight", 0.) > 0)
                ImGui::TextWrapped("Clip mix: %.1f%%", status.value("blend_weight", 0.) * 100);
        } else
            ImGui::TextWrapped(
                "%s", status.is_object()
                          ? status.value("error", "Waiting for animation status...").c_str()
                          : "Waiting for animation status...");
        auto send = [&](const nlohmann::json& value) {
            if (submit(value))
                error_.clear();
            else
                error_ = "Play is busy. Try again when the controls are ready.";
        };
        ImGui::BeginDisabled(!ready || pending || !available);
        if (ui::button(playing ? "Pause clip" : "Resume clip",
                       "Change this clip's playback intent. Game pause remains unchanged."))
            send({{"operation", playing ? "pause" : "play"}});
        FORGE_UI_PROBE("live-animation:pause");
        const auto& style = ImGui::GetStyle();
        if (ImGui::GetContentRegionAvail().x > ImGui::CalcTextSize("Restart clip").x +
                                                   style.FramePadding.x * 2 +
                                                   ImGui::GetItemRectSize().x + style.ItemSpacing.x)
            ImGui::SameLine();
        if (ui::button(
                "Restart clip",
                "Reset this player and resume its clip without moving the actor across the jump."))
            send({{"operation", "restart"}});
        FORGE_UI_PROBE("live-animation:restart");
        const double zero = 0;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderScalar("##playhead", ImGuiDataType_Double, &position_, &zero, &duration,
                                "%.3f s", ImGuiSliderFlags_AlwaysClamp))
            dirty_ = true;
        const bool released = ImGui::IsItemDeactivatedAfterEdit();
        FORGE_UI_PROBE("live-animation:playhead");
        ui::help("Release to seek. Retains clip pause and fades; skipped markers and root movement "
                 "are not replayed.");
        if (dirty_ && released && ready && !pending && available) {
            send({{"operation", "seek"}, {"position_seconds", position_}});
            dirty_ = false;
        }
        ImGui::EndDisabled();
        if (pending)
            ImGui::TextUnformatted("Applying animation playback...");
        if (!error.empty())
            ImGui::TextWrapped("Animation playback rejected: %s", error.c_str());
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

  private:
    EntityRef source_{};
    std::string session_, error_;
    double position_ = 0;
    bool dirty_ = false;
};
} // namespace forge
