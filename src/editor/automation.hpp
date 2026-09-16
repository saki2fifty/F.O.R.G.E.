#pragma once
#include "document.hpp"
#include "widgets.hpp"
#include <SDL3/SDL_clipboard.h>
#include <forge/live_authoring.hpp>
namespace forge::ui {
class AutomationWorkspace {
  public:
    bool open = false;
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
        generation_ = document.generation();
        notice_.clear();
    }
    void pump(SceneDocument& document, const std::string& busy) {
        if (!live_.active())
            return;
        try {
            document.check_ownership();
            if (generation_ != document.generation()) {
                live_.stop();
                notice_ = "Connection stopped because the active document changed. Start a new "
                          "connection when ready.";
                return;
            }
            live_.pump(busy);
        } catch (const std::exception& e) {
            live_.stop();
            notice_ = e.what();
        }
    }
    void draw(Scene& scene, SceneDocument& document, const std::string& busy) {
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
            help("Why the previous connection stopped. Scene switches revoke old access.");
            if (!live_.active()) {
                ImGui::BeginDisabled(!busy.empty());
                try {
                    if (button("Start read only",
                               "Allow schema discovery, scene reads, queries and diagnostics."))
                        start(scene, document, false);
                    if (button(
                            "Start with scene edits",
                            "Also allow validated scene commands and undo/redo. Changes are "
                            "unsaved editor edits; no native execution or file tools are exposed."))
                        start(scene, document, true);
                } catch (const std::exception& e) {
                    notice_ = e.what();
                }
                ImGui::EndDisabled();
            } else {
                ImGui::Text("127.0.0.1:%u | %s", unsigned(live_.port()),
                            live_.editable() ? "Scene edits allowed" : "Read only");
                help("Only this machine can connect. The per-connection secret is also required.");
                if (button("Copy connection JSON",
                           "Copy host, port and a secret token. Paste only into trusted local "
                           "tools. Stop revokes this token; it is never saved in project files.")) {
                    if (!SDL_SetClipboardText(live_.connection().dump().c_str()))
                        notice_ = SDL_GetError();
                    else
                        notice_ = "Connection copied. Treat its token as a password.";
                }
                if (button("Stop connection", "Disconnect clients and revoke access immediately. "
                                              "Committed edits remain undoable."))
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
            help("See the User Manual: Live automation. This connection uses FORGE JSON requests; "
                 "the MCP adapter is still planned.");
        }
        ImGui::End();
    }
    LiveAuthoring& connection() { return live_; }

  private:
    LiveAuthoring live_;
    std::uint64_t generation_ = 0;
    std::string notice_;
};
} // namespace forge::ui
