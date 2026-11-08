#pragma once
#include "document.hpp"
#include "property_drawer.hpp"
#include <algorithm>
#include <cmath>
#include <forge/authoring.hpp>
#include <forge/sound_cue.hpp>
#include <functional>

namespace forge {
// Transient creation intent. The published cue and its child Sources own all data.
class SoundPlaylistEditor {
  public:
    using Lookup = std::function<const AssetRecord*(AssetId)>;
    bool active() const { return open_; }
    void open(const Scene& scene, const SceneDocument& document, std::vector<AssetId> clips,
              const Lookup& lookup) {
        if (clips.empty() || clips.size() > sound_cue_child_limit)
            throw std::runtime_error("Select 1 to 8 AudioClip assets in Content.");
        for (const auto id : clips) {
            const auto* record = lookup(id);
            if (!record || record->type != "audio_clip")
                throw std::runtime_error("Select registered AudioClip assets only.");
        }
        project_ = document.project();
        generation_ = document.generation();
        scene_ = scene.asset_id();
        revision_ = scene.revision();
        clips_ = std::move(clips);
        name_ = "Sound playlist";
        gap_ = 0;
        loop_ = false;
        startup_ = pause_ = true;
        error_.clear();
        open_ = request_ = true;
    }
    void draw(Scene& scene, SceneDocument& document, const Lookup& lookup, bool locked,
              const std::function<void(const std::string&)>& select) {
        if (!open_)
            return;
        constexpr auto title = "Create sound playlist";
        if (request_) {
            ImGui::OpenPopup(title);
            request_ = false;
        }
        const auto available = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize({std::min(620.f * ui::interface_scale, available.x - 30),
                                  std::min(590.f * ui::interface_scale, available.y - 30)},
                                 ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal(title, nullptr))
            return;
        const bool same = project_ == document.project() && generation_ == document.generation() &&
                          scene_ == scene.asset_id();
        const bool stale = revision_ != scene.revision();
        ImGui::BeginChild("Playlist settings", {0, std::max(80.f, ImGui::GetContentRegionAvail().y -
                                                                      45.f * ui::interface_scale)});
        ImGui::TextWrapped("Create an Ordered Sound Cue and child Audio Sources in this Scene. "
                           "Each sound finishes before the next starts. One Undo step.");
        ImGui::InputText("Name", &name_);
        FORGE_UI_PROBE("sound-playlist:name");
        for (std::size_t i = 0; i < clips_.size(); ++i) {
            ui::IdScope row(std::to_string(i).c_str());
            const auto* record = same ? lookup(clips_[i]) : nullptr;
            ImGui::TextWrapped("%zu. %s", i + 1,
                               record ? asset_display(*record).c_str() : "Missing AudioClip");
            int direction = 0;
            ImGui::BeginDisabled(i == 0);
            if (ui::button("Move up", "Play this clip earlier in the list."))
                direction = -1;
            FORGE_UI_PROBE("sound-playlist:up:" + std::to_string(i));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 == clips_.size());
            if (ui::button("Move down", "Play this clip later in the list."))
                direction = 1;
            FORGE_UI_PROBE("sound-playlist:down:" + std::to_string(i));
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool remove = ui::button("Remove", "Remove only this entry from the draft.");
            FORGE_UI_PROBE("sound-playlist:remove:" + std::to_string(i));
            if (remove) {
                clips_.erase(clips_.begin() + std::ptrdiff_t(i));
                break;
            }
            if (direction) {
                std::swap(clips_[i], clips_[std::size_t(std::ptrdiff_t(i) + direction)]);
                break;
            }
        }
        ImGui::InputDouble("Gap (seconds)", &gap_, 0, 0, "%.3f");
        FORGE_UI_PROBE("sound-playlist:gap");
        ImGui::Checkbox("Loop playlist", &loop_);
        FORGE_UI_PROBE("sound-playlist:loop");
        ImGui::Checkbox("Play on start", &startup_);
        FORGE_UI_PROBE("sound-playlist:startup");
        ImGui::Checkbox("Pause with game", &pause_);
        FORGE_UI_PROBE("sound-playlist:game-pause");
        const bool valid = !clips_.empty() && clips_.size() <= sound_cue_child_limit &&
                           name_.find_first_not_of(" \t\r\n") != std::string::npos &&
                           std::isfinite(gap_) && gap_ >= 0 &&
                           gap_ <= sound_cue_repeat_max_seconds &&
                           std::all_of(clips_.begin(), clips_.end(), [&](AssetId id) {
                               const auto* record = same ? lookup(id) : nullptr;
                               return record && record->type == "audio_clip";
                           });
        if (!same)
            ImGui::TextWrapped("This draft belongs to another project or Scene. Cancel and "
                               "open it again from Content.");
        else if (stale) {
            ImGui::TextWrapped("The Scene changed. Review its current state before creating.");
            if (ui::button("Use current Scene state",
                           "Keep this clip list and refresh its target revision."))
                revision_ = scene.revision();
            FORGE_UI_PROBE("sound-playlist:refresh");
        }
        if (!valid && same)
            ImGui::TextWrapped(
                "Use a name, 1 to 8 registered AudioClips and a gap from 0 to 3600 seconds.");
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        ImGui::EndChild();
        ImGui::BeginDisabled(locked || !same || stale || !valid);
        const bool create = ui::button(
            "Create in Scene", "Create the cue and Sources together. Save Scene to keep them.");
        FORGE_UI_PROBE("sound-playlist:create");
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool cancel = ui::button("Cancel", "Discard this draft without changing the Scene.");
        FORGE_UI_PROBE("sound-playlist:cancel");
        if (create) {
            try {
                document.check_ownership();
                Json clips = Json::array();
                for (const auto id : clips_) {
                    const auto* record = lookup(id);
                    if (!record || record->type != "audio_clip")
                        throw std::runtime_error(
                            "An AudioClip changed; reopen the playlist draft.");
                    clips.push_back({{"clip", id}, {"name", asset_display(*record)}});
                }
                const auto result =
                    apply_authoring(scene,
                                    Json::array({{{"operation", "audio.playlist_create"},
                                                  {"arguments",
                                                   {{"clips", clips},
                                                    {"name", name_},
                                                    {"gap_seconds", gap_},
                                                    {"loop", loop_},
                                                    {"play_on_start", startup_},
                                                    {"pause_with_game", pause_}}}}}),
                                    revision_);
                select(result.at("results")[0].at("selected"));
                open_ = false;
                ImGui::CloseCurrentPopup();
            } catch (const std::exception& error) {
                error_ = error.what();
            }
        } else if (cancel) {
            open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

  private:
    bool open_ = false, request_ = false, loop_ = false, startup_ = true, pause_ = true;
    std::filesystem::path project_;
    AssetId scene_;
    std::uint64_t generation_ = 0, revision_ = 0;
    std::vector<AssetId> clips_;
    std::string name_, error_;
    double gap_ = 0;
};
} // namespace forge
