#pragma once
#include "automation_permissions.hpp"
#include "document_automation.hpp"
#include "files.hpp"
#include "mcp.hpp"
#include "widgets.hpp"
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_filesystem.h>
#include <forge/live_authoring.hpp>
namespace forge::ui {
class AutomationWorkspace {
  public:
    bool open = false;
    std::function<std::vector<LiveAuthoringMethod>(const AutomationPermissions&)> workflow_methods;
    bool active() const { return live_.active(); }
    void menu() {
        if (ImGui::BeginMenu(live_.active() ? "Automation (on)" : "Automation")) {
            if (ImGui::MenuItem("Local connection..."))
                open = true;
            help("Inspect or edit this scene through a connection you explicitly enable.");
            if (live_.active() && ImGui::MenuItem("Stop connection"))
                live_.stop();
            help("Close the listener, disconnect clients, and revoke its access token.");
            ImGui::EndMenu();
        }
        help("Local authoring API controls. Connections start off and never enable themselves.");
    }
    void start(Scene& scene, SceneDocument& document, bool edits) {
        document.check_ownership();
        live_.start(scene, edits);
        scene_ = &scene;
        project_ = document.project();
        document_writes_ = false;
        generation_ = document.generation();
        scope_project_ = document.project();
        permissions_ = {};
        notice_.clear();
    }
    void start(Scene& scene, EditorFiles& files, bool edits, bool document_writes = false,
               AutomationPermissions permissions = {}) {
        auto& document = files.document;
        document.check_ownership();
        if (!permissions.workspace_root.empty()) {
            permissions.workspace_root = std::filesystem::canonical(permissions.workspace_root);
            if (!std::filesystem::is_directory(permissions.workspace_root))
                throw std::runtime_error("Choose an existing project workspace folder");
        }
        auto methods = document_automation_methods(
            document, document_writes,
            [&files](const std::filesystem::path& path) { files.automation_save(path); },
            [&files](const std::filesystem::path& path, bool discard) {
                files.automation_switch({path.empty() ? EditorFiles::Command::NewScene
                                                      : EditorFiles::Command::OpenScene,
                                         path,
                                         {}},
                                        discard);
            });
        if (workflow_methods) {
            auto extra = workflow_methods(permissions);
            methods.insert(methods.end(), std::make_move_iterator(extra.begin()),
                           std::make_move_iterator(extra.end()));
        }
        live_.start(scene, edits, std::move(methods));
        permissions_ = std::move(permissions);
        scope_project_ = document.project();
        scene_ = &scene;
        project_ = document.project();
        document_writes_ = document_writes;
        generation_ = document.generation();
        notice_.clear();
    }
    void pump(SceneDocument& document, const std::string& busy) {
        if (auto result = root_picker_.take()) {
            if (!result->error.empty())
                notice_ = result->error;
            else if (!result->path.empty()) {
                if (picking_workspace_)
                    SDL_strlcpy(workspace_root_text_, result->path.c_str(),
                                sizeof(workspace_root_text_));
                else
                    SDL_strlcpy(import_root_text_, result->path.c_str(), sizeof(import_root_text_));
            }
        }
        if (!live_.active())
            return;
        try {
            document.check_ownership();
            if (!synchronize(document))
                return;
            live_.pump(busy);
            synchronize(document);
        } catch (const std::exception& e) {
            live_.stop();
            notice_ = e.what();
        }
    }
    void draw(Scene& scene, EditorFiles& files, const std::string& busy,
              SDL_Window* window = nullptr) {
        if (!open)
            return;
        ImGui::SetNextWindowSize({520 * interface_scale, 360 * interface_scale},
                                 ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Local automation", &open)) {
            heading("Connection", "A loopback-only API for trusted local scripts. It shares the "
                                  "editor scene and undo history.");
            ImGui::TextWrapped("Project writer: owned by this editor");
            help("A second FORGE editor cannot open this project until this one closes or switches "
                 "projects.");
            ImGui::TextWrapped("%s", live_.status().c_str());
            help("Latest connection result. Credentials and request contents are not logged here.");
            if (!notice_.empty())
                ImGui::TextWrapped("%s", notice_.c_str());
            help("Scene grants expire on scene switches. File grants last within this project.");
            if (!live_.active()) {
                ImGui::BeginDisabled(!busy.empty() || root_picker_.busy());
                try {
                    if (button("Start read only",
                               "Allow schema discovery, scene reads, queries and diagnostics."))
                        start(scene, files, false);
                    if (button("Start with scene edits",
                               "Also allow validated scene commands and undo/redo. Changes are "
                               "unsaved editor edits."))
                        start(scene, files, true);
                    if (button("Start with scene and file edits",
                               "Also allow Save, Save As, New and Open Scene inside this project. "
                               "This grant survives scene switches; project switches revoke it."))
                        start(scene, files, true, true);
                    if (workflow_methods) {
                        ImGui::Separator();
                        ImGui::Checkbox("Allow an external source folder", &external_import_);
                        if (external_import_) {
                            ImGui::InputText("Import folder", import_root_text_,
                                             sizeof(import_root_text_));
                            if (button("Browse source folder...",
                                       "Choose the only external folder from which an agent may "
                                       "import raw assets.")) {
                                picking_workspace_ = false;
                                root_picker_.show(FileDialog::Kind::ProjectParent, window,
                                                  import_root_text_);
                            }
                        }
                        ImGui::Checkbox("Allow a project workspace", &workspace_access_);
                        if (workspace_access_) {
                            ImGui::InputText("Projects folder", workspace_root_text_,
                                             sizeof(workspace_root_text_));
                            if (button("Browse projects folder...",
                                       "Permit project creation/opening inside this folder, in "
                                       "addition to the initially connected project.")) {
                                picking_workspace_ = true;
                                root_picker_.show(FileDialog::Kind::ProjectParent, window,
                                                  workspace_root_text_);
                            }
                        }
                        if (button(
                                "Start project authoring",
                                "Allow project content and source authoring. A selected "
                                "Projects folder also permits project creation/opening there.")) {
                            AutomationPermissions permissions;
                            permissions.assets = true;
                            permissions.code = true;
                            if (workspace_access_) {
                                if (!workspace_root_text_[0])
                                    throw std::runtime_error(
                                        "Choose an existing project workspace folder");
                                permissions.workspace_root =
                                    std::filesystem::u8path(workspace_root_text_);
                            }
                            if (external_import_) {
                                if (!std::filesystem::is_directory(
                                        std::filesystem::u8path(import_root_text_)))
                                    throw std::runtime_error(
                                        "Choose an existing readable import folder");
                                permissions.import_root = std::filesystem::canonical(
                                    std::filesystem::u8path(import_root_text_));
                            }
                            start(scene, files, true, true, std::move(permissions));
                        }
                    }
                } catch (const std::exception& e) {
                    notice_ = e.what();
                }
                ImGui::EndDisabled();
            } else {
                ImGui::Text("127.0.0.1:%u | %s", unsigned(live_.port()),
                            permissions_.assets ? "Project authoring allowed"
                            : document_writes_  ? "Scene and file edits allowed"
                            : live_.editable()  ? "Scene edits allowed"
                                                : "Read only");
                help("Only this machine can connect. The per-connection secret is also required.");
                if (button("Copy connection JSON",
                           "Copy host, port and a secret token. Paste only into trusted local "
                           "tools. Stop revokes this token; it is never saved in project files.")) {
                    if (!SDL_SetClipboardText(live_.connection().dump().c_str()))
                        notice_ = SDL_GetError();
                    else
                        notice_ = "Connection copied. Treat its token as a password.";
                }
                if (button("Copy MCP configuration",
                           "Copy a local MCP server configuration for an AI client. It contains "
                           "this connection's secret; Stop connection revokes access.")) {
                    const auto* base = SDL_GetBasePath();
                    if (!base)
                        notice_ = "Cannot locate the FORGE tools folder.";
                    else {
#ifdef _WIN32
                        const auto tools = std::filesystem::u8path(base) / "forge_tools.exe";
#else
                        const auto tools = std::filesystem::u8path(base) / "forge_tools";
#endif
                        const auto config = mcp_client_configuration(tools, live_.connection());
                        if (!SDL_SetClipboardText(config.dump(2).c_str()))
                            notice_ = SDL_GetError();
                        else
                            notice_ = "MCP configuration copied. Add it to your trusted AI client. "
                                      "It includes a temporary secret; Stop connection revokes it.";
                    }
                }
                if (button("Stop connection", "Disconnect clients and revoke access immediately. "
                                              "Committed scene edits and file saves remain."))
                    live_.stop();
            }
            if (!busy.empty())
                ImGui::TextWrapped("Edits paused: %s", busy.c_str());
            help("Automation edits pause during play, builds, file dialogs, popups and active UI "
                 "gestures.");
            ImGui::Separator();
            ImGui::TextWrapped(
                "Use Examples/Automation/live_scene.py with Python 3 to inspect this scene or add "
                "an undoable four-shape example. Save changes normally in the editor.");
            help("The MCP adapter in forge_tools connects AI clients to this same editor service. "
                 "See the User Manual: MCP agents. Current tools cover scene authoring and "
                 "granted document, asset and settings operations. Project switching requires "
                 "the workspace grant. C++ build, Play and export tools remain unavailable.");
        }
        ImGui::End();
    }
    LiveAuthoring& connection() { return live_; }

  private:
    bool synchronize(SceneDocument& document) {
        bool allowed_project = document.project() == scope_project_;
        if (!allowed_project && !permissions_.workspace_root.empty())
            try {
                const ProjectPaths workspace(permissions_.workspace_root);
                if (workspace.root() == permissions_.workspace_root) {
                    (void)workspace.relative(document.project());
                    allowed_project = true;
                }
            } catch (const std::exception&) {
            }
        if (!allowed_project || (generation_ != document.generation() && !document_writes_)) {
            live_.stop();
            notice_ = "Connection stopped because its document/project scope changed. Start a "
                      "new connection when ready.";
            return false;
        }
        if (generation_ != document.generation() || project_ != document.project()) {
            live_.retarget(*scene_);
            project_ = document.project();
            generation_ = document.generation();
        }
        return true;
    }
    LiveAuthoring live_;
    Scene* scene_ = nullptr;
    std::filesystem::path project_, scope_project_;
    bool document_writes_ = false;
    AutomationPermissions permissions_;
    FileDialog root_picker_;
    bool external_import_ = false;
    bool workspace_access_ = false, picking_workspace_ = false;
    char import_root_text_[2048]{};
    char workspace_root_text_[2048]{};
    std::uint64_t generation_ = 0;
    std::string notice_;
};
} // namespace forge::ui
