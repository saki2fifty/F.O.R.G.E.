#pragma once
#include "document.hpp"
#include "property_drawer.hpp"
#include <forge/animation_conversion.hpp>
#include <forge/authoring.hpp>
#include <future>
namespace forge {
class AnimationTools {
  public:
    bool pending() const { return job_.valid(); }
    explicit AnimationTools(std::filesystem::path executable) : converter_(std::move(executable)) {}
    ~AnimationTools() { cancel_.request_stop(); }
    void poll(SceneDocument& project, std::string& message) {
        if (!job_.valid())
            return;
        if (project.project() != project_)
            cancel_.request_stop();
        if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try {
            auto candidate = job_.get();
            if (cancel_.stop_requested() || project.project() != project_)
                throw std::runtime_error("Animation conversion cancelled");
            project.check_ownership();
            candidate.publish();
            message = "Animation assets registered. Select an object and choose Skeleton / "
                      "Animation clip in its Animator.";
        } catch (const std::exception& e) {
            message = e.what();
            ui::report_error("animation_tools.hpp", message);
        }
    }
    void content(SceneDocument& project, bool locked, std::string& message) {
        if (!ImGui::CollapsingHeader("Animation assets")) {
            ui::help("Convert project-contained glTF skeletons and clips with the packaged Ozz "
                     "converter.");
            return;
        }
        ui::help("Conversion publishes a validated skeleton and clip set together. Asset changes "
                 "are separate from scene Undo; restart Play after reconversion.");
        ImGui::BeginDisabled(locked || job_.valid());
        ImGui::InputText("glTF in project", source_, sizeof(source_));
        ui::help("Animation-only .gltf with named clips, no images or extensions. Relative buffer "
                 "files must remain inside this project.");
        if (ui::button("Convert/Register Animation",
                       "Run the packaged converter in a bounded worker. Failed conversion keeps "
                       "all previous usable assets.")) {
            try {
                project.check_ownership();
                project_ = project.project();
                cancel_ = std::stop_source{};
                auto root = project_, source = std::filesystem::u8path(source_), tool = converter_;
                auto token = cancel_.get_token();
                job_ = std::async(std::launch::async, [root, source, tool, token] {
                    return prepare_animation_conversion(root, source, tool, token);
                });
                message = "Converting animation...";
            } catch (const std::exception& e) {
                message = e.what();
                ui::report_error("animation_tools.hpp", message);
            }
        }
        ImGui::EndDisabled();
        if (job_.valid()) {
            ImGui::TextUnformatted("Converting / validating...");
            ui::help("The converter has a 30-second wall-time limit. The catalog changes only "
                     "after validation.");
            if (ui::button("Cancel conversion",
                           "Stop the worker and discard its unpublished candidate."))
                cancel_.request_stop();
        }
    }

  private:
    std::filesystem::path converter_, project_;
    char source_[1024] = "Assets/two-joints.gltf";
    std::stop_source cancel_;
    std::future<AnimationCandidate> job_;
};
} // namespace forge
