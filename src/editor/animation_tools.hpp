#pragma once
#include "audio_inspector.hpp"
#include <forge/animation_conversion.hpp>
#include <future>
namespace forge {
inline void animation_inspector(Scene& scene, SceneDocument& project, const std::string& selected,
                                std::string& message) {
    if (selected.empty())
        return;
    try {
        Json item;
        const auto effective = scene.effective_document();
        for (const auto& e : effective.at("entities"))
            if (e.at("id") == selected)
                item = e;
        if (item.is_null())
            return;
        if (ImGui::TreeNode("Animation")) {
            struct Scope {
                ~Scope() { ImGui::TreePop(); }
            } scope;
            ui::help("Single-clip skeletal playback in Play. Shows joints and bones; character "
                     "mesh rendering is not available yet.");
            constexpr const char* key = "forge.animator";
            if (!item.at("components").contains(key)) {
                if (ui::button("Add Animator",
                               "Add an optional Animator in one undoable scene edit."))
                    authoring_command(scene, "component.add",
                                      {{"entity", selected}, {"component", key}});
            } else {
                auto schema = scene.schema();
                for (const auto& type : schema.at("components"))
                    if (type.at("id") == key)
                        for (const auto& field : type.at("fields")) {
                            const std::string name = field.at("id");
                            auto value = item["components"][key][name];
                            if (audio_field(project.project(), field, value))
                                authoring_command(scene, "property.set",
                                                  {{"entity", selected},
                                                   {"component", key},
                                                   {"field", name},
                                                   {"value", value}});
                        }
                if (ui::button("Remove / Revert Animator",
                               "Remove owned Animator configuration or its prefab overrides; scene "
                               "Undo restores this edit."))
                    authoring_command(scene, "component.revert",
                                      {{"entity", selected}, {"component", key}});
                ImGui::TextWrapped(
                    "Convert a glTF in Content > Animation assets. Play shows the sampled bones; "
                    "Pause freezes them and Step advances one simulation tick.");
                ui::help("Prefab property overrides and Revert use the existing Inspector prefab "
                         "controls. Animation assets are separate from scene Undo.");
            }
        } else
            ui::help("Add and configure skeletal animation for this object.");
    } catch (const std::exception& e) {
        message = e.what();
    }
}
class AnimationTools {
  public:
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
