#pragma once
#include "document.hpp"
#include "widgets.hpp"
namespace forge {
class ProjectSettingsEditor {
  public:
    void open() { open_ = true; }
    void menu() {
        if (ImGui::MenuItem("Project Settings"))
            open_ = true;
        ui::help("Shared simulation frequency, startup scene and input actions. Personal layout "
                 "and UI scale remain user preferences.");
    }
    void draw(SceneDocument& document, Scene& scene, bool locked, std::string& status) {
        if (!open_) {
            loaded_ = false;
            return;
        }
        if (!loaded_ || root_ != document.project()) {
            draft_ = baseline_ = document.settings().document();
            root_ = document.project();
            loaded_ = true;
        }
        ImGui::SetNextWindowSize({560 * ui::interface_scale, 530 * ui::interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Project Settings", &open_)) {
            ImGui::BeginDisabled(locked);
            double hz = draft_.value("simulation_hz", 60.0);
            if (ImGui::InputDouble("Simulation Hz", &hz, 1, 10, "%.1f"))
                draft_["simulation_hz"] = hz;
            ui::help("Shared project frequency, 1..240 Hz. Takes effect on the next Play. Catch-up "
                     "limits and interpolation policy remain unchanged.");
            if (ui::button("Use saved current scene as startup",
                           "Store the saved scene's AssetId. Moving its file within this project "
                           "does not change its identity.")) {
                if (!document.on_disk() || document.path().empty())
                    status = "Save this scene before choosing it as startup";
                else
                    draft_["startup_scene"] = {
                        {"asset", scene.asset_id()},
                        {"source",
                         path_utf8(document.path().lexically_relative(document.project()))}};
            }
            ui::heading("Input actions",
                        "Project-owned stable action identities. Rename labels without changing "
                        "the identity used by runtime consumers.");
            if (ui::button("Add action",
                           "Create a digital action with a Space binding. Rename it and choose "
                           "digital, one-dimensional or two-dimensional values.")) {
                if (draft_["input"]["actions"].size() < 64)
                    draft_["input"]["actions"].push_back(
                        {{"id", ActionId::generate()},
                         {"name", "New action"},
                         {"kind", "digital"},
                         {"bindings",
                          Json::array(
                              {{{"control", "key.space"}, {"x", 1}, {"y", 0}, {"deadzone", 0}}})}});
            }
            auto& actions = draft_["input"]["actions"];
            int remove = -1;
            for (std::size_t i = 0; i < actions.size(); ++i) {
                auto& a = actions[i];
                ImGui::PushID(a.at("id").get_ref<const std::string&>().c_str());
                if (ImGui::TreeNode("Action", "%s",
                                    a.at("name").get_ref<const std::string&>().c_str())) {
                    ui::help("Action configuration. Expanding does not change bindings.");
                    char name[129]{};
                    SDL_strlcpy(name, a.at("name").get<std::string>().c_str(), sizeof(name));
                    if (ImGui::InputText("Name", name, sizeof(name)))
                        a["name"] = name;
                    ui::help("Display label only; stable UUID stays unchanged.");
                    if (ImGui::BeginCombo("Kind",
                                          a.at("kind").get_ref<const std::string&>().c_str())) {
                        for (const char* k : {"digital", "axis1", "axis2"}) {
                            if (ImGui::Selectable(k, a.at("kind") == k)) {
                                a["kind"] = k;
                                if (std::string(k) != "axis2")
                                    for (auto& b : a["bindings"]) {
                                        b["y"] = 0;
                                        if (std::string(k) == "digital") {
                                            b["x"] = 1;
                                            b["deadzone"] = 0;
                                        }
                                    }
                            }
                            ui::help("Digital held/edges, scalar axis, or two-axis vector. Digital "
                                     "actions require button/key bindings.");
                        }
                        ImGui::EndCombo();
                    }
                    ui::help("The action's fixed-tick value shape.");
                    int remove_binding = -1;
                    auto& bindings = a["bindings"];
                    for (std::size_t b = 0; b < bindings.size(); ++b) {
                        auto& binding = bindings[b];
                        ImGui::PushID(static_cast<int>(b));
                        if (ImGui::BeginCombo(
                                "Control",
                                binding.at("control").get_ref<const std::string&>().c_str())) {
                            for (const auto& c : input_controls())
                                if (a.at("kind") != "digital" || c.digital) {
                                    if (ImGui::Selectable(c.id.c_str(),
                                                          binding.at("control") == c.id)) {
                                        binding["control"] = c.id;
                                        binding["deadzone"] = 0;
                                    }
                                    ui::help("Physical control on keyboard, mouse or the first "
                                             "connected gamepad. Escape/F6/F7 are reserved for "
                                             "editor capture and clock controls.");
                                }
                            ImGui::EndCombo();
                        }
                        ui::help("FORGE control identifier; no SDL numeric code is persisted.");
                        for (const char* field : {"x", "y", "deadzone"}) {
                            if (a.at("kind") == "digital" ||
                                (std::string(field) == "y" && a.at("kind") != "axis2"))
                                continue;
                            const auto& control =
                                input_control(binding.at("control").get<std::string>());
                            if (std::string(field) == "deadzone" &&
                                (control.digital || control.relative))
                                continue;
                            double v = binding.value(field, std::string(field) == "x" ? 1.0 : 0.0);
                            if (ImGui::InputDouble(field, &v, 0, 0, "%.3f"))
                                binding[field] = v;
                            ui::help("x/y are contribution scales. Use -1 for opposite directions. "
                                     "y requires axis2. Axis deadzone must be at least zero and "
                                     "below one.");
                        }
                        if (ui::button("Remove binding", "Remove this binding from the candidate. "
                                                         "Save settings publishes the change."))
                            remove_binding = static_cast<int>(b);
                        ImGui::PopID();
                    }
                    if (remove_binding >= 0)
                        bindings.erase(bindings.begin() + remove_binding);
                    if (ui::button("Add binding",
                                   "Add another control contribution to this action.")) {
                        if (bindings.size() < 16)
                            bindings.push_back(
                                {{"control", "key.space"}, {"x", 1}, {"y", 0}, {"deadzone", 0}});
                    }
                    if (ui::button("Remove action",
                                   "Remove this action from project configuration. Existing "
                                   "gameplay code referencing its ID must handle absence."))
                        remove = static_cast<int>(i);
                    ImGui::TreePop();
                } else
                    ui::help("Expand to edit the action's label, kind and bindings.");
                ImGui::PopID();
            }
            if (remove >= 0)
                actions.erase(actions.begin() + remove);
            if (ui::button("Save settings",
                           "Validate and atomically save this project manifest. Changes take "
                           "effect next Play. Scene Undo does not own project settings.")) {
                try {
                    document.save_settings(draft_, &baseline_);
                    baseline_ = draft_;
                    status = "Project settings saved; next Play uses this configuration";
                } catch (const std::exception& e) {
                    status = e.what();
                }
            }
            ImGui::SameLine();
            if (ui::button("Discard edits",
                           "Restore the current loaded project settings without writing files."))
                draft_ = baseline_ = document.settings().document();
            ImGui::EndDisabled();
            ImGui::TextWrapped("Play > Capture gameplay input sends controls to your actions. Esc "
                               "releases. F6 pauses/resumes; F7 steps. Console > Gameplay input "
                               "shows consumed values and edge counts.");
            ui::help("Input actions are a foundation; binding an action does not automatically add "
                     "a player controller or gameplay behavior.");
        }
        ImGui::End();
        if (!open_)
            loaded_ = false;
    }

  private:
    bool open_ = false, loaded_ = false;
    std::filesystem::path root_;
    Json draft_, baseline_;
};
} // namespace forge
