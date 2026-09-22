#pragma once
#include "document.hpp"
#include "file_dialog.hpp"
#include "ui_probe.hpp"
#include "widgets.hpp"
#include <functional>
namespace forge {
class EditorFiles {
  public:
    enum class Command { NewScene, OpenScene, OpenProject, CreateProject, Quit, RecoverUntitled };
    struct Action {
        Command command;
        std::filesystem::path path;
        std::string name;
    };
    SceneDocument document;
    std::string status = "Ready", error;
    bool changed = false, preferences_changed = false, quit = false;
    EditorFiles(Scene& scene, SDL_Window* window, std::vector<std::string>& recent)
        : document(scene), window_(window), recent_(recent) {}
    void start(const std::filesystem::path& path) {
        document.open_project(path, true);
        SDL_strlcpy(parent_, path_text(document.project().parent_path()).c_str(), sizeof(parent_));
        recovery_untitled_ = !document.has_recovery() && document.has_untitled_recovery();
        recovery_prompt_ = document.has_recovery() || recovery_untitled_;
        remember();
    }
    std::function<bool()> external_busy;
    bool busy() const {
        return dialog_.busy() || pending_.has_value() || (external_busy && external_busy());
    }
    std::function<bool(const Action&)> before_request;
    std::function<void()> save_active;
    void request(Action action) {
        if (before_request && !before_request(action))
            return;
        if (busy())
            return;
        if (!can_switch_ && action.command != Command::Quit) {
            status = "Wait for the native build before switching scenes or projects";
            return;
        }
        pending_ = std::move(action);
        if (document.dirty())
            unsaved_prompt_ = true;
        else
            execute();
    }
    void save(bool save_as = false) {
        if (external_busy && external_busy()) {
            status = "Finish the Content file operation before saving";
            return;
        }
        try {
            if (save_as || document.path().empty()) {
                dialog_.show(FileDialog::Kind::SaveScene, window_,
                             path_text(document.path().empty()
                                           ? document.project() / "Scenes/Untitled.scene.json"
                                           : document.path()));
            } else {
                document.save();
                status = "Scene saved";
                if (pending_)
                    execute();
            }
        } catch (const std::exception& e) {
            status = error = e.what();
        }
    }
    enum class Resolution { Save, Discard, Cancel };
    void resolve_pending(Resolution choice) {
        if (choice == Resolution::Cancel) {
            pending_.reset();
            unsaved_prompt_ = false;
            status = "Operation cancelled";
        } else if (choice == Resolution::Save) {
            save();
        } else {
            const auto ownership = document.writer_guard();
            const auto old_recovery = document.recovery_path();
            if (execute()) {
                std::error_code ignored;
                std::filesystem::remove(old_recovery, ignored);
            }
        }
    }
    void set_switch_available(bool available) { can_switch_ = available; }
    void pump(bool can_switch) {
        can_switch_ = can_switch;
        if (auto result = dialog_.take()) {
            if (!result->error.empty())
                status = result->error;
            if (result->path.empty()) {
                if (pending_)
                    unsaved_prompt_ = true;
            } else {
                try {
                    const auto path = std::filesystem::u8path(result->path);
                    switch (result->kind) {
                    case FileDialog::Kind::ImportFiles:
                        throw std::runtime_error("Source imports belong to the Content workflow");
                    case FileDialog::Kind::OpenScene:
                        request({Command::OpenScene, path, {}});
                        break;
                    case FileDialog::Kind::OpenProject:
                        request({Command::OpenProject, path, {}});
                        break;
                    case FileDialog::Kind::ProjectParent:
                        SDL_strlcpy(parent_, result->path.c_str(), sizeof(parent_));
                        break;
                    case FileDialog::Kind::SaveScene:
                        document.save_as(
                            path.extension().empty()
                                ? std::filesystem::u8path(path_text(path) + ".scene.json")
                                : path);
                        status = "Scene saved";
                        if (pending_)
                            execute();
                        break;
                    }
                } catch (const std::exception& e) {
                    status = error = e.what();
                    if (pending_)
                        unsaved_prompt_ = true;
                }
            }
        }
        const auto now = SDL_GetTicks();
        if (!(external_busy && external_busy()) && now - last_autosave_ >= 30000) {
            last_autosave_ = now;
            try {
                if (document.autosave())
                    status = "Recovery snapshot saved (your scene file is unchanged)";
            } catch (const std::exception& e) {
                status = error = e.what();
            }
        }
    }
    void menu(const std::function<void()>& save_menu = {}, bool scene_write = true) {
        const bool open = ImGui::BeginMenu("File");
        FORGE_UI_PROBE("menu:File");
        if (open) {
            ui::help("Projects, scene files, and recovery snapshots.");
            ImGui::BeginDisabled(busy());
            if (entry("New project...", "Create a new project folder with Scenes, Assets, Native, "
                                        "and a versioned project manifest."))
                create_prompt_ = true;
            if (entry(
                    "Open project...",
                    "Select a project folder. Existing main.scene.json folders remain supported."))
                dialog_.show(FileDialog::Kind::OpenProject, window_, path_text(document.project()));
            if (ImGui::BeginMenu("Recent projects")) {
                for (const auto& path : recent_) {
                    if (entry(path.c_str(), "Open this project. Missing folders produce a "
                                            "diagnostic without replacing the current scene.")) {
                        request({Command::OpenProject, std::filesystem::u8path(path), {}});
                        break;
                    }
                }
                ImGui::EndMenu();
            }
            ui::help("The last eight successfully opened projects.");
            ImGui::Separator();
            if (entry("New scene", "Create an unsaved empty scene. Ctrl+N."))
                request({Command::NewScene, {}, {}});
            if (entry("Open scene...", "Choose a scene inside this project. Ctrl+O."))
                open_scene_dialog();
            if (entry("Reload from disk",
                      "Reload the current scene file after resolving unsaved edits.")) {
                if (!document.path().empty())
                    request({Command::OpenScene, document.path(), {}});
                else
                    status = "Untitled scenes have no disk file to reload";
            }
            if (save_menu)
                save_menu();
            else if (entry("Save", "Save the active task. Ctrl+S.")) {
                if (save_active)
                    save_active();
                else
                    save();
            }
            ImGui::BeginDisabled(!scene_write);
            if (entry("Save scene As...",
                      "Save to another JSON scene file inside this project. Ctrl+Shift+S."))
                save(true);
            ImGui::EndDisabled();
            ImGui::Separator();
            if (entry("Create recovery snapshot", "Write unsaved edits to the recovery file now; "
                                                  "the scene file remains unchanged.")) {
                try {
                    status = document.autosave() ? "Recovery snapshot saved"
                                                 : "No new unsaved edits to snapshot";
                } catch (const std::exception& e) {
                    status = error = e.what();
                }
            }
            if (entry("Recover current scene", "Restore a matching autosave as an undoable edit. "
                                               "Does not overwrite the scene file.")) {
                try {
                    document.recover();
                    status = "Recovery restored; inspect and Save to keep it";
                } catch (const std::exception& e) {
                    status = error = e.what();
                }
            }
            if (entry("Recover untitled scene",
                      "Restore an autosaved scene that had not yet been given a filename."))
                request({Command::RecoverUntitled, {}, {}});
            if (entry("Exit", "Close the editor after resolving unsaved changes."))
                request({Command::Quit, {}, {}});
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        ui::help("Project and scene file operations, recent projects, and autosave recovery.");
    }
    void shortcuts(bool scene_write = true) {
        const auto& io = ImGui::GetIO();
        if (busy() ||
            ImGui::IsPopupOpen(nullptr,
                               ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
            io.WantTextInput || ImGui::IsAnyItemActive() || !io.KeyCtrl)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            if (!io.KeyShift && save_active)
                save_active();
            else if (scene_write)
                save(io.KeyShift);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_O, false))
            open_scene_dialog();
        if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            request({Command::NewScene, {}, {}});
    }
    void draw_dialogs() {
        if (create_prompt_) {
            ImGui::OpenPopup("Create project");
            create_prompt_ = false;
        }
        if (ImGui::BeginPopupModal("Create project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name / folder", name_, sizeof(name_));
            ui::help("Name of the new project and its new subfolder. Existing folders are not "
                     "overwritten.");
            ImGui::InputText("Parent folder", parent_, sizeof(parent_));
            ui::help("Existing parent directory in which to create the project.");
            ImGui::BeginDisabled(dialog_.busy());
            if (ui::button("Browse...", "Choose the parent folder using the system folder dialog."))
                dialog_.show(FileDialog::Kind::ProjectParent, window_, parent_);
            if (ui::button(
                    "Create",
                    "Create the complete project in a staging directory, then activate it.")) {
                const std::string name = name_;
                if (name.empty() || name == "." || name == ".." ||
                    name.find_first_of("/\\:<>\"|?*") != std::string::npos || name.back() == '.' ||
                    name.back() == ' ')
                    status = "Choose a valid folder name without path separators or trailing "
                             "dots/spaces";
                else {
                    request({Command::CreateProject,
                             std::filesystem::u8path(parent_) / std::filesystem::u8path(name),
                             name});
                    ImGui::CloseCurrentPopup();
                }
            }
            if (ui::button("Cancel", "Keep the current project."))
                ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s", status.c_str());
            ui::help("Latest project or file operation result.");
            ImGui::EndPopup();
        }
        if (unsaved_prompt_) {
            ImGui::OpenPopup("Unsaved scene");
            unsaved_prompt_ = false;
        }
        if (ImGui::BeginPopupModal("Unsaved scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("This scene has unsaved changes.");
            ui::help("Save, discard, or cancel before replacing the authored scene or closing the "
                     "editor.");
            if (ui::button("Save and continue",
                           "Save successfully before performing the requested operation.")) {
                resolve_pending(Resolution::Save);
                if (!pending_ || dialog_.busy())
                    ImGui::CloseCurrentPopup();
            }
            if (ui::button("Discard changes", "Discard authored changes and this scene's recovery "
                                              "snapshot, then continue.")) {
                try {
                    resolve_pending(Resolution::Discard);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    status = error = e.what();
                }
            }
            FORGE_UI_PROBE("unsaved:discard");
            if (ui::button("Cancel", "Keep working on the current scene.")) {
                resolve_pending(Resolution::Cancel);
                ImGui::CloseCurrentPopup();
            }
            FORGE_UI_PROBE("unsaved:cancel");
            ImGui::TextWrapped("%s", status.c_str());
            ui::help("Save failures leave the scene and pending operation available.");
            ImGui::EndPopup();
        }
        if (recovery_prompt_) {
            ImGui::OpenPopup("Recovery available");
            recovery_prompt_ = false;
        }
        if (ImGui::BeginPopupModal("Recovery available", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(recovery_untitled_
                                       ? "An unsaved untitled scene can be recovered."
                                       : "An autosave exists for this scene.");
            ui::help(
                "Recovery is offered explicitly; the on-disk scene is never silently replaced.");
            if (ui::button("Restore recovery",
                           "Restore only if the snapshot matches this scene's disk baseline.")) {
                try {
                    if (recovery_untitled_) {
                        document.recover_untitled();
                        changed = true;
                    } else
                        document.recover();
                    status = "Recovery restored; inspect and Save to keep it";
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    status = error = e.what();
                }
            }
            if (ui::button("Keep saved scene",
                           "Delete this recovery snapshot and keep the saved scene.")) {
                try {
                    document.discard_recovery(recovery_untitled_);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    status = error = e.what();
                }
            }
            if (ui::button("Later", "Keep the recovery file; access it again from File."))
                ImGui::CloseCurrentPopup();
            ImGui::TextWrapped("%s", status.c_str());
            ui::help("Recovery errors leave the snapshot intact for inspection.");
            ImGui::EndPopup();
        }
    }

  private:
    static bool entry(const char* label, const char* description) {
        const std::string name = label;
        const char* shortcut = name == "New scene"          ? "Ctrl+N"
                               : name == "Open scene..."    ? "Ctrl+O"
                               : name == "Save"             ? "Ctrl+S"
                               : name == "Save scene As..." ? "Ctrl+Shift+S"
                                                            : nullptr;
        const bool clicked = ImGui::MenuItem(label, shortcut);
        FORGE_UI_PROBE("file:" + name);
        ui::help(description);
        return clicked;
    }
    void open_scene_dialog() {
        dialog_.show(FileDialog::Kind::OpenScene, window_, path_text(document.project()));
    }
    void remember() {
        const auto path = path_text(document.project());
        std::erase(recent_, path);
        recent_.insert(recent_.begin(), path);
        if (recent_.size() > 8)
            recent_.resize(8);
        preferences_changed = true;
    }
    bool execute() {
        if (!pending_)
            return false;
        const auto action = *pending_;
        pending_.reset();
        if (!can_switch_ && action.command != Command::Quit) {
            status = "Wait for the native build before switching";
            return false;
        }
        try {
            switch (action.command) {
            case Command::Quit:
                quit = true;
                return true;
            case Command::NewScene:
                document.new_scene();
                break;
            case Command::OpenScene:
                document.open_scene(action.path);
                break;
            case Command::OpenProject:
                document.open_project(action.path);
                remember();
                break;
            case Command::CreateProject:
                SceneDocument::create_project(action.path, action.name);
                document.open_project(action.path);
                remember();
                break;
            case Command::RecoverUntitled:
                document.recover_untitled();
                break;
            }
            changed = true;
            recovery_untitled_ = !document.has_recovery() && document.has_untitled_recovery();
            recovery_prompt_ = action.command != Command::RecoverUntitled &&
                               (document.has_recovery() || recovery_untitled_);
            status = "Opened " + document.name() + " / " +
                     (document.path().empty() ? "Untitled" : path_text(document.path().filename()));
            return true;
        } catch (const std::exception& e) {
            status = error = e.what();
            return false;
        }
    }
    SDL_Window* window_;
    std::vector<std::string>& recent_;
    FileDialog dialog_;
    std::optional<Action> pending_;
    bool recovery_untitled_ = false;
    bool can_switch_ = true, create_prompt_ = false, unsaved_prompt_ = false,
         recovery_prompt_ = false;
    Uint64 last_autosave_ = 0;
    char name_[128] = "MyGame", parent_[2048]{};
};
} // namespace forge
