#include "document_workspace.hpp"
#include "project_automation.hpp"
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void check(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
Json invoke(const std::vector<LiveAuthoringMethod>& methods, const char* name,
            const Json& request = Json::object()) {
    const auto method = std::find_if(methods.begin(), methods.end(),
                                     [&](const auto& value) { return value.name == name; });
    check(method != methods.end(), "Settings method missing");
    return method->invoke(request);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need settings scratch directory and case");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        const auto first = root / "First", second = root / "Second";
        SceneDocument::create_project(first, "First");
        SceneDocument::create_project(second, "Second");
        EngineContext engine;
        Scene scene(engine.world());
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent);
        files.start(first);
        const auto saved = ProjectSettings(first).document();
        ProjectSettingsEditor settings;
        std::string status;
        ui::DocumentWorkspace documents;
        documents.add({"settings",
                       "Project Settings",
                       "Project Settings###Project Settings",
                       false,
                       [&] { return settings.is_open(); },
                       [&] { return settings.dirty(); },
                       [&] { settings.draw(files.document, scene, false, status); },
                       [&] { settings.request_save(); },
                       {},
                       {},
                       [&] { settings.request_close(); }});
        files.automation_unavailable = [&](bool switching) -> std::string {
            return switching && documents.source_drafts_dirty() ? "Settle the settings draft" : "";
        };
        ui::AutomationPermissions permissions;
        permissions.assets = true;
        const auto methods =
            ProjectAutomation::settings_methods(settings, files.document, permissions);
        auto read = [&] { return invoke(methods, "project.settings.read"); };
        ImGui::CreateContext();
        struct UiCleanup {
            ~UiCleanup() {
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
        } ui_cleanup;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1100, 900};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        ui::style(1);
        ui::EditorUiContext context;
        ui::ContextScope scope(context);
        test::observe_ui = true;
        auto frame = [&] {
            test::ui_targets.clear();
            ImGui::NewFrame();
            documents.draw();
            ImGui::Render();
        };
        const auto mode = std::string_view(argv[2]);
        if (mode == "analog" || mode == "touch") {
            const bool touch = mode == "touch";
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            const auto id = ActionId::generate();
            const Json input{
                {"version", 1},
                {"actions", Json::array({{{"id", id},
                                          {"name", touch ? "Touch pressure" : "Analog aim"},
                                          {"kind", touch ? "digital" : "axis1"},
                                          {"bindings", Json::array({{{"control", "pad.left_x"},
                                                                     {"deadzone", .2}}})}}})}};
            invoke(methods, "project.settings.edit",
                   {{"state", read().at("state")},
                    {"patch",
                     Json::array({{{"op", "replace"}, {"path", "/input"}, {"value", input}}})}});
            const auto click = [&](const std::string& key) {
                for (int retry = 0; retry < 4; ++retry) {
                    frame();
                    const auto box = test::ui_targets.at(key);
                    const auto x = (box.minimum.x + box.maximum.x) * .5f;
                    const auto y = (box.minimum.y + box.maximum.y) * .5f;
                    if (y < box.clip_minimum.y || y > box.clip_maximum.y) {
                        auto* window =
                            ImGui::FindWindowByName("Project Settings###Project Settings");
                        check(window != nullptr, "Analog settings window missing");
                        ImGui::SetScrollY(window,
                                          window->Scroll.y + y -
                                              (box.clip_minimum.y + box.clip_maximum.y) * .5f);
                        continue;
                    }
                    check(box.enabled && x >= box.clip_minimum.x && x <= box.clip_maximum.x,
                          "Analog setting control clipped or disabled");
                    io.AddMousePosEvent(x, y);
                    frame();
                    io.AddMouseButtonEvent(0, true);
                    frame();
                    io.AddMouseButtonEvent(0, false);
                    frame();
                    return;
                }
                throw std::runtime_error("Analog setting could not be scrolled into view");
            };
            const auto edit = [&](const std::string& key, const char* text) {
                click(key);
                io.AddKeyEvent(ImGuiMod_Ctrl, true);
                io.AddKeyEvent(ImGuiKey_A, true);
                frame();
                io.AddKeyEvent(ImGuiKey_A, false);
                io.AddKeyEvent(ImGuiMod_Ctrl, false);
                io.AddInputCharactersUTF8(text);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, true);
                frame();
                io.AddKeyEvent(ImGuiKey_Enter, false);
                frame();
            };
            frame();
            click("settings:input:action:" + id.str());
            if (touch) {
                for (float scale : {1.f, 1.5f, 2.f}) {
                    ui::style(scale);
                    frame();
                    click("settings:input:control");
                    io.AddKeyEvent(ImGuiKey_End, true);
                    frame();
                    io.AddKeyEvent(ImGuiKey_End, false);
                    frame();
                    io.AddKeyEvent(ImGuiKey_Enter, true);
                    frame();
                    io.AddKeyEvent(ImGuiKey_Enter, false);
                    frame();
                    const auto binding =
                        read().at("draft").at("input").at("actions")[0].at("bindings")[0];
                    check(binding.at("control") == "touch.7.pressure",
                          "Actual Settings combo did not select the final touch control");
                    check(!test::ui_targets.contains("settings:input:negative-direction"),
                          "Touch pressure offers a meaningless negative direction");
                }
                check(settings.save(files.document, status), "Touch settings could not be saved");
                check(ProjectSettings(first).input().actions()[0].bindings[0].control ==
                          "touch.7.pressure",
                      "Selected touch control did not survive settings reopen");
                std::cout
                    << "Native Settings touch control selection/range/Save passed at3scales\n";
                return 0;
            }
            for (float scale : {1.f, 1.5f, 2.f}) {
                ui::style(scale);
                frame();
                edit("settings:input:outer-deadzone", "0.1");
                edit("settings:input:response-exponent", "2");
                const auto binding =
                    read().at("draft").at("input").at("actions")[0].at("bindings")[0];
                check(binding.at("outer_deadzone") == .1 && binding.at("response_exponent") == 2,
                      "Actual UI input did not edit analog response fields");
            }
            edit("settings:input:outer-deadzone", "0.9");
            check(!settings.save(files.document, status) && settings.dirty(),
                  "Conflicting deadzones were saved");
            check(ProjectSettings(first).document() == saved,
                  "Failed response save changed project");
            edit("settings:input:outer-deadzone", "0.1");
            check(settings.save(files.document, status),
                  "Valid analog settings could not be saved");
            const auto persisted = ProjectSettings(first).input().source();
            check(persisted.at("actions")[0].at("bindings")[0].at("response_exponent") == 2,
                  "Analog response did not survive reopen");
        } else if (mode == "defaults") {
            frame(); // Native workspace draws closed owners too.
            const auto created =
                invoke(methods, "project.settings.game_defaults", {{"state", read().at("state")}});
            check(created.at("draft").contains("game") && created.at("dirty").get<bool>(),
                  "Defaults were not created in the draft");
            frame();
            const auto current = read();
            check(settings.is_open() && current.at("draft") == created.at("draft") &&
                      settings.dirty(),
                  "UI frame discarded automation-created game defaults");
            files.changed = false;
            bool blocked = false;
            try {
                files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
            } catch (const std::exception&) {
                blocked = true;
            }
            check(blocked && files.document.project() == first,
                  "Defaults draft bypassed project switch guard");
            invoke(methods, "project.settings.save", {{"state", current.at("state")}});
            frame();
            check(!settings.dirty() && ProjectSettings(first).document().at("game") ==
                                           created.at("draft").at("game"),
                  "Explicit Save did not preserve the generated game identity/defaults");
        } else if (mode == "diagnostic") {
            settings.open();
            frame();
            settings.set_frequency(0);
            check(!settings.save(files.document, status) && settings.dirty(),
                  "Invalid settings save unexpectedly succeeded");
            const auto failed = read();
            check(failed.at("diagnostic") != "", "Rejected save lost its diagnostic");
            settings.request_close();
            check(!settings.resolve_close(ui::DraftResolution::Cancel, files.document, status) &&
                      settings.dirty() && read().at("diagnostic") == failed.at("diagnostic"),
                  "Cancel discarded the failed draft or its diagnostic");
            // Exercise the in-window discard control independently of closing.
            frame();
            auto* window = ImGui::FindWindowByName("Project Settings###Project Settings");
            check(window != nullptr, "Settings window missing");
            ImGui::SetScrollY(window, window->ScrollMax.y);
            frame();
            const auto box = test::ui_targets.at("button:Discard edits");
            const auto x = (box.minimum.x + box.maximum.x) * .5f;
            const auto y = (box.minimum.y + box.maximum.y) * .5f;
            check(x >= box.clip_minimum.x && x <= box.clip_maximum.x && y >= box.clip_minimum.y &&
                      y <= box.clip_maximum.y,
                  "Discard control was clipped");
            io.AddMousePosEvent(x, y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            check(!settings.dirty() && read().at("diagnostic") == "" && settings.is_open(),
                  "Discard edits retained the failed draft/diagnostic");
            settings.set_frequency(0);
            check(!settings.save(files.document, status),
                  "Repeated invalid settings save succeeded");
            settings.request_close();
            check(settings.resolve_close(ui::DraftResolution::Discard, files.document, status),
                  "Discard failed");
            frame();
            check(!settings.dirty() && read().at("diagnostic") == "",
                  "Discarded settings retained the obsolete diagnostic");
            settings.open();
            frame();
            check(read().at("diagnostic") == "" && read().at("draft") == saved,
                  "Reopening settings retained a failed draft/error");
            settings.request_close();
            files.changed = false;
            files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
            invoke(methods, "project.settings.game_defaults", {{"state", read().at("state")}});
            frame();
            check(read().at("diagnostic") == "" && read().at("draft").at("name") == "Second",
                  "New project settings inherited the prior diagnostic/draft");
            check(ProjectSettings(first).document() == saved, "Discard published invalid settings");
        } else {
            check(mode == "queued", "Unknown settings lifecycle case");
            settings.open();
            frame();
            settings.request_save();
            settings.request_close();
            frame();
            bool blocked = false;
            try {
                invoke(methods, "project.settings.edit",
                       {{"state", read().at("state")},
                        {"patch",
                         Json::array(
                             {{{"op", "replace"}, {"path", "/name"}, {"value", "New draft"}}})}});
            } catch (const AuthoringError& error) {
                blocked = error.code == "document_busy";
            }
            check(!blocked && settings.dirty(), "Closed settings retained a stale queued Save");
            frame();
            check(ProjectSettings(first).document() == saved && settings.dirty(),
                  "Old queued Save published the replacement draft");
        }
        check(!scene.can_undo() && !files.document.dirty(),
              "Settings operation changed scene history");
        std::cout << "Project Settings " << mode << " lifecycle passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
