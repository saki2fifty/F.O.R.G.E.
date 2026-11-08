#pragma once
#include "../input_recording_files.hpp"
#include "document.hpp"
#include "play.hpp"
#include "widgets.hpp"
#include <misc/cpp/imgui_stdlib.h>

namespace forge::ui {
class InputRecordingControls {
  public:
    void draw(PlaySession& play, SceneDocument& document,
              const std::function<void()>& start_paused) {
        const auto& project = document.project();
        if (project_ != project) {
            project_ = project;
            pending_.clear();
            error_.clear();
            saved_.clear();
            replace_ = false;
            replay_ = nullptr;
        }
        try {
            poll(play, document);
        } catch (const std::exception& error) {
            pending_.clear();
            error_ = error.what();
        }
        const bool open = ImGui::TreeNode("Input recording");
        FORGE_UI_PROBE("input-recording:section");
        if (!open)
            return;
        ImGui::TextWrapped(
            "Record fixed-tick gameplay actions from a fresh Play launch. "
            "Replay supplies the same actions; game-state determinism is not guaranteed.");
        ImGui::BeginDisabled(!pending_.empty());
        ImGui::InputText("Project file", &path_);
        help("Use Recordings/<name>.json. Saving creates the Recordings folder. UI/text input and "
             "Scene "
             "transitions are outside this recording profile.");
        ImGui::Checkbox("Replace existing recording", &replace_);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(play.active() || !pending_.empty());
        if (ImGui::Button("Record input"))
            attempt([&] {
                document.check_ownership();
                input_recording_path(project_, std::filesystem::u8path(path_));
                start_paused();
                if (!play.active())
                    throw std::runtime_error("Play did not start");
                saved_.clear();
                pending_ = "begin";
                launch_ = play.launch_id();
            });
        FORGE_UI_PROBE("input-recording:record");
        ImGui::SameLine();
        if (ImGui::Button("Replay input"))
            attempt([&] {
                document.check_ownership();
                replay_ = read_input_recording(project_, std::filesystem::u8path(path_));
                start_paused();
                if (!play.active())
                    throw std::runtime_error("Play did not start");
                saved_.clear();
                pending_ = "replay";
                launch_ = play.launch_id();
            });
        FORGE_UI_PROBE("input-recording:replay");
        ImGui::EndDisabled();
        const bool same_project = play.input_recording_project(project);
        const auto status = same_project ? play.input_recording_status() : Json::object();
        const auto mode = status.value("mode", "idle");
        ImGui::Text("%s | %llu ticks", mode.c_str(),
                    static_cast<unsigned long long>(status.value("ticks", std::uint64_t{})));
        const auto notice = status.value("notice", "");
        if (!notice.empty())
            ImGui::TextWrapped("%s", notice.c_str());
        ImGui::BeginDisabled(!play.control_ready() || play.input_recording_pending() ||
                             status.value("ticks", std::uint64_t{}) == 0 || !pending_.empty() ||
                             (mode != "recording" && mode != "recorded" && mode != "failed"));
        if (ImGui::Button("Finish and save input"))
            attempt([&] {
                if (mode == "recording" && !play.submit_input_recording("finish"))
                    throw std::runtime_error("Wait for the current Play control");
                pending_ = "fetch";
                launch_ = play.launch_id();
            });
        FORGE_UI_PROBE("input-recording:finish");
        ImGui::EndDisabled();
        if (same_project && !play.input_recording_document().is_null()) {
            if (ImGui::Button("Save input file"))
                attempt([&] { save(play, document); });
            FORGE_UI_PROBE("input-recording:save");
        }
        if (same_project && !play.input_recording_error().empty())
            ImGui::TextWrapped("%s", play.input_recording_error().c_str());
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
        if (!saved_.empty())
            ImGui::TextWrapped("%s", saved_.c_str());
        ImGui::TreePop();
    }

  private:
    template <class F> void attempt(F&& action) {
        try {
            action();
            error_.clear();
        } catch (const std::exception& error) {
            pending_.clear();
            error_ = error.what();
        }
    }
    void save(PlaySession& play, SceneDocument& document) {
        document.check_ownership();
        if (document.project() != project_ || !play.input_recording_project(project_))
            throw std::runtime_error("The retrieved recording belongs to another project");
        write_input_recording(project_, std::filesystem::u8path(path_),
                              play.input_recording_document(), replace_);
        saved_ = "Saved " + path_;
    }
    void poll(PlaySession& play, SceneDocument& document) {
        if (pending_.empty())
            return;
        if (!play.active() || play.launch_id() != launch_)
            throw std::runtime_error("Input recording stopped before completion");
        if (!play.input_recording_error().empty())
            throw std::runtime_error(play.input_recording_error());
        if (!play.control_ready() || play.input_recording_pending())
            return;
        if (pending_ == "begin" || pending_ == "replay") {
            if (!play.submit_input_recording(pending_, replay_))
                throw std::runtime_error("Fresh input recording was not admitted");
            pending_ = "resume";
        } else if (pending_ == "resume") {
            const auto mode = play.input_recording_status().value("mode", "idle");
            if (mode != "recording" && mode != "replaying")
                throw std::runtime_error("Runtime did not acknowledge input recording");
            play.resume();
            pending_.clear();
        } else if (pending_ == "fetch") {
            if (!play.submit_input_recording("read"))
                throw std::runtime_error("Input recording is not ready to save");
            pending_ = "save";
        } else if (pending_ == "save") {
            save(play, document);
            pending_.clear();
        }
    }
    std::filesystem::path project_;
    std::string path_ = "Recordings/input-recording.json", pending_, launch_, error_, saved_;
    Json replay_;
    bool replace_ = false;
};
} // namespace forge::ui
