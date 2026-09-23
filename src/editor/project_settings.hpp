#pragma once
#include "document.hpp"
#include "editor_state.hpp"
#include "widgets.hpp"
#include <forge/game_settings.hpp>
namespace forge {
class ProjectSettingsEditor {
  public:
    void open() {
        open_ = true;
        focus_requested_ = true;
    }
    bool dirty() const { return loaded_ && draft_ != baseline_; }
    bool is_open() const { return open_; }
    bool close_cancelled = false;
    void request_close() {
        if (dirty())
            close_requested_ = true;
        else {
            open_ = false;
            loaded_ = false;
        }
    }
    void request_save() { save_requested_ = true; }
    void set_frequency(double hz) {
        if (loaded_)
            draft_["simulation_hz"] = hz;
    }
    bool resolve_close(ui::DraftResolution choice, SceneDocument& document, std::string& status) {
        if (choice == ui::DraftResolution::Cancel) {
            close_requested_ = false;
            close_cancelled = true;
            return false;
        }
        if (choice == ui::DraftResolution::Save && !save(document, status))
            return false;
        draft_ = baseline_;
        close_requested_ = false;
        open_ = false;
        loaded_ = false;
        return true;
    }
    bool save(SceneDocument& document, std::string& status) {
        try {
            document.save_settings(draft_, &baseline_);
            baseline_ = draft_;
            error_.clear();
            status = "Project settings saved; next Play uses this configuration";
            return true;
        } catch (const std::exception& e) {
            status = error_ = e.what();
            return false;
        }
    }
    void menu() {
        if (ImGui::MenuItem("Project Settings"))
            open();
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
        ui::draft_window_size({560 * ui::interface_scale, 530 * ui::interface_scale});
        if (focus_requested_) {
            ImGui::SetNextWindowFocus();
            focus_requested_ = false;
        }
        bool visible = true;
        const auto title = std::string(dirty() ? "* " : "") + "Project Settings###Project Settings";
        const bool expanded = ImGui::Begin(title.c_str(), &visible);
        if (!visible)
            request_close();
        if (ui::editor_context)
            ui::editor_context->task.focus(ui::DocumentTask::Settings);
        if (save_requested_ && !locked) {
            save_requested_ = false;
            save(document, status);
        }
        if (expanded) {
            ImGui::TextWrapped("%s | Save Settings owns this draft. Scene Undo does not edit it.",
                               dirty() ? "Unsaved settings" : "Saved settings");
            if (ui::button("Close Settings",
                           "Close this task; unsaved settings prompt Save, Discard or Cancel."))
                request_close();
            ui::help("Ctrl+S saves settings when this task is active. Closing unsaved settings "
                     "asks Save, Discard or Cancel.");
            ImGui::BeginDisabled(locked);
            double hz = draft_.value("simulation_hz", 60.0);
            if (ImGui::InputDouble("Simulation Hz", &hz, 1, 10, "%.1f"))
                set_frequency(hz);
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
            ui::heading("Standalone game", "Shared defaults for exported games. Player overrides "
                                           "and saves use OS user-data storage.");
            if (!draft_.contains("game")) {
                if (ui::button("Set up game defaults",
                               "Create a persistent application key and editable defaults; Save "
                               "Settings to publish."))
                    draft_["game"] = default_game_settings(
                        "forge.game." + AssetId::generate().str(), draft_.at("name"));
            } else if (ImGui::TreeNodeEx("Game defaults", ImGuiTreeNodeFlags_DefaultOpen)) {
                auto& game = draft_["game"];
                auto text = [&](const char* label, Json& value, const char* help) {
                    char buffer[257]{};
                    SDL_strlcpy(buffer, value.get<std::string>().c_str(), sizeof(buffer));
                    if (ImGui::InputText(label, buffer, sizeof(buffer)))
                        value = buffer;
                    ui::help(help);
                };
                text("Game title", game["title"],
                     "Shown on the game window; does not change asset identities.");
                text("Application ID", game["application_id"],
                     "Stable lowercase dotted key for saves/settings. Changing it selects a "
                     "different OS user-data directory.");
                ImGui::TextUnformatted("Runtime profile: Development");
                ui::help("Current exporter supports Development standalone. Shipping is not "
                         "offered yet.");
                auto& display = game["display"];
                if (ImGui::BeginCombo("Window mode",
                                      display.at("mode").get_ref<const std::string&>().c_str())) {
                    for (const char* mode : {"windowed", "borderless", "fullscreen"}) {
                        if (ImGui::Selectable(mode, display.at("mode") == mode))
                            display["mode"] = mode;
                        ui::help("Initial mode; supported user preferences override this default.");
                    }
                    ImGui::EndCombo();
                }
                ui::help("Windowed, borderless desktop, or exclusive fullscreen.");
                for (const char* field : {"width", "height", "display"}) {
                    int value = display.at(field).get<int>();
                    const char* label = std::string_view(field) == "width"    ? "Width (px)"
                                        : std::string_view(field) == "height" ? "Height (px)"
                                                                              : "Display index";
                    if (ImGui::InputInt(label, &value))
                        display[field] = value;
                    ui::help("Initial resolution or zero-based display preference. Validated when "
                             "saving.");
                }
                bool vsync = display.at("vsync");
                if (ImGui::Checkbox("VSync", &vsync))
                    display["vsync"] = vsync;
                ui::help("Synchronize standalone presentation to the display. Editor viewport "
                         "settings are independent.");
                float volume = game["audio"]["master_volume"];
                if (ImGui::SliderFloat("Master volume", &volume, 0, 1))
                    game["audio"]["master_volume"] = volume;
                ui::help("Initial output gain from silent to full volume.");
                double sensitivity = game["input"]["mouse_sensitivity"];
                if (ImGui::InputDouble("Mouse sensitivity", &sensitivity, .1, 1, "%.2f"))
                    game["input"]["mouse_sensitivity"] = sensitivity;
                ui::help("Initial mouse input multiplier, 0.01 to 100.");
                ImGui::Text("Save schema: %d", game.at("save_schema").get<int>());
                ui::help("Save schema changes need a matching gameplay migration; not a cosmetic "
                         "version number.");
                ImGui::TreePop();
            }
            ui::heading("Physics",
                        "Shared project gravity. Applies when starting the next Play runtime.");
            auto gravity = draft_.value("physics", Json{{"version", 1}, {"gravity", {0, -9.81, 0}}})
                               .at("gravity")
                               .get<Double3>();
            if (ui::xyz_input("Gravity (m/s^2)", gravity.data(),
                              "World acceleration in meters per second squared; +Y is up.")) {
                if (!draft_.contains("physics"))
                    draft_["physics"] = {{"version", 1}};
                draft_["physics"]["gravity"] = gravity;
            }
            ui::help("Acceleration in meters per second squared. +Y is up; default Y is -9.81. "
                     "Scene Undo does not change project settings.");
            ui::heading("Input actions",
                        "Project-owned stable action identities. Rename labels without changing "
                        "the identity used by runtime consumers.");
            ImGui::BeginDisabled(draft_["input"]["actions"].size() >= 64);
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
            ImGui::EndDisabled();
            if (draft_["input"]["actions"].size() >= 64)
                ImGui::TextWrapped("Action limit reached (64). Remove an action to add another.");
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
                    ImGui::BeginDisabled(bindings.size() >= 16);
                    if (ui::button("Add binding",
                                   "Add another control contribution to this action.")) {
                        if (bindings.size() < 16)
                            bindings.push_back(
                                {{"control", "key.space"}, {"x", 1}, {"y", 0}, {"deadzone", 0}});
                    }
                    ImGui::EndDisabled();
                    if (bindings.size() >= 16)
                        ImGui::TextWrapped("Binding limit reached (16).");
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
            if (ui::button("Save Settings",
                           "Validate and atomically save this project manifest. Changes take "
                           "effect next Play. Scene Undo does not own project settings.")) {
                save(document, status);
            }
            ImGui::SameLine();
            if (ui::button("Discard edits",
                           "Restore the current loaded project settings without writing files."))
                draft_ = baseline_ = document.settings().document();
            ImGui::EndDisabled();
            ImGui::TextWrapped("Game > Capture gameplay input sends controls to your actions. Esc "
                               "releases. F6 pauses/resumes; F7 steps. Console > Gameplay input "
                               "shows consumed values and edge counts.");
            ui::help("Input actions are a foundation; binding an action does not automatically add "
                     "a player controller or gameplay behavior.");
        }
        if (!error_.empty()) {
            ui::field_error(error_);
            ui::report_error("project-settings", error_);
        }
        ImGui::End();
        if (close_requested_)
            ImGui::OpenPopup("Unsaved Project Settings");
        if (ImGui::BeginPopupModal("Unsaved Project Settings", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "Save project settings before closing? Scene Save does not save this draft.");
            ui::help("Save validates this project manifest. Discard affects only the draft; Cancel "
                     "keeps editing.");
            ImGui::BeginDisabled(locked);
            bool finish = false;
            if (ui::button("Save Settings",
                           "Validate and save this project; failure retains the draft."))
                finish = resolve_close(ui::DraftResolution::Save, document, status);
            if (ui::button("Discard", "Discard unpublished project settings."))
                finish = resolve_close(ui::DraftResolution::Discard, document, status);
            ImGui::EndDisabled();
            if (ui::button("Cancel", "Keep editing and cancel the pending close or switch.")) {
                resolve_close(ui::DraftResolution::Cancel, document, status);
                ImGui::CloseCurrentPopup();
            }
            if (!error_.empty())
                ui::field_error(error_);
            if (finish) {
                close_requested_ = false;
                open_ = false;
                loaded_ = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (!open_)
            loaded_ = false;
    }

  private:
    bool open_ = false, loaded_ = false, close_requested_ = false, save_requested_ = false,
         focus_requested_ = false;
    std::string error_;
    std::filesystem::path root_;
    Json draft_, baseline_;
};
} // namespace forge
