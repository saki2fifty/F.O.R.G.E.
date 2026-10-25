#include "automation.hpp"
#include "editor/game_export.hpp"
#include "editor_state.hpp"
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void check(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need picker scratch directory");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        SceneDocument::create_project(root / "Game", "Picker");
        EngineContext engine;
        Scene scene(engine.world());
        std::string long_path = "C:/folder with spaces/";
        for (unsigned depth = 0; depth < 12; ++depth) {
            for (unsigned i = 0; i < 64; ++i)
                long_path += "\xe7\x95\x8c"; // UTF-8; each component stays below 255 bytes.
            long_path += "/";
        }
        long_path += "chosen-folder";
        std::vector<std::string> recent;
        EditorFiles files(scene, nullptr, recent, std::filesystem::u8path(long_path));
        files.start(root / "Game");
        ui::AutomationWorkspace workspace;
        workspace.open = true;
        workspace.workflow_methods = [](const ui::AutomationPermissions&) {
            return std::vector<LiveAuthoringMethod>{};
        };
        ui::GameExportTask exporter;
        bool show_export = false, show_project = false, open_project_dialog = false;
        ImGui::CreateContext();
        struct UiCleanup {
            ~UiCleanup() {
                test::observe_ui = false;
                FileDialog::fixture_selection.reset();
                ImGui::DestroyContext();
            }
        } ui_cleanup;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200, 1100};
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
            workspace.pump(files.document, "");
            files.pump(true);
            test::ui_targets.clear();
            ImGui::NewFrame();
            if (show_export)
                exporter.draw(nullptr, files.document, scene, false, false, [] {});
            else if (show_project) {
                if (open_project_dialog) {
                    ImGui::OpenPopup("Create project");
                    open_project_dialog = false;
                }
                files.draw_dialogs();
            } else
                workspace.draw(scene, files, "");
            ImGui::Render();
        };
        frame();
        frame();
        ImGui::SetWindowSize(ImGui::FindWindowByName("Local automation"), {900, 1000});
        frame();
        auto click = [&](const std::string& name) {
            const auto item = test::ui_targets.at(name);
            check(item.enabled, "Picker control disabled");
            const float x = (item.minimum.x + item.maximum.x) * .5f;
            const float y = (item.minimum.y + item.maximum.y) * .5f;
            check(x >= item.clip_minimum.x && x <= item.clip_maximum.x &&
                      y >= item.clip_minimum.y && y <= item.clip_maximum.y,
                  "Picker control clipped");
            io.AddMousePosEvent(x, y);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
        };
        auto text = [&](const std::string& name) {
            click(name);
            auto* input = ImGui::GetInputTextState(test::ui_targets.at(name).id);
            check(input != nullptr, "Folder text field did not activate");
            return std::string(input->GetText());
        };
        auto type = [&](const std::string& name, const std::string& value) {
            click(name);
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
            io.AddKeyEvent(ImGuiKey_A, true);
            frame();
            io.AddKeyEvent(ImGuiKey_A, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
            frame();
            io.AddInputCharactersUTF8(value.c_str());
            frame();
            check(text(name) == value, "Typing a long folder path truncated its value");
        };
        const auto before = scene.document();
        struct Field {
            const char *allow, *browse, *input;
        };
        const Field fields[] = {{"automation:allow-import", "button:Browse source folder...",
                                 "automation:import-folder"},
                                {"automation:allow-workspace", "button:Browse projects folder...",
                                 "automation:workspace-folder"},
                                {"automation:allow-output", "button:Browse output folder...",
                                 "automation:output-folder"}};
        for (const auto& field : fields) {
            click(field.allow);
            const auto deliveries = FileDialog::fixture_deliveries;
            FileDialog::fixture_selection =
                FileDialog::Result{FileDialog::Kind::ProjectParent, long_path, {}, {long_path}};
            click(field.browse);
            check(FileDialog::fixture_deliveries == deliveries + 1,
                  "Actual Browse action did not consume the picker result");
            check(text(field.input) == long_path,
                  "Folder selection was truncated before automation admission");
            const auto typed = long_path + "/typed";
            type(field.input, typed);
            FileDialog::fixture_selection =
                FileDialog::Result{FileDialog::Kind::ProjectParent, {}, {}, {}};
            click(field.browse);
            check(text(field.input) == typed, "Cancelling the picker changed the prior folder");
            check(!workspace.active(), "Selecting a folder enabled automation");
        }
        show_project = open_project_dialog = true;
        frame();
        frame();
        check(text("project:parent-folder") == long_path, "Initial project parent was truncated");
        FileDialog::fixture_selection =
            FileDialog::Result{FileDialog::Kind::ProjectParent, long_path + "/new-parent", {}, {}};
        click("button:Browse...");
        check(text("project:parent-folder") == long_path + "/new-parent",
              "Selected project parent was truncated");
        type("project:parent-folder", long_path + "/typed-parent");
        click("button:Cancel");
        show_project = false;
        show_export = true;
        exporter.open();
        frame();
        frame();
        for (const auto* label : {"Destination", "Runtime kit"}) {
            FileDialog::fixture_selection =
                FileDialog::Result{FileDialog::Kind::ProjectParent, long_path, {}, {}};
            click(std::string("export:browse:") + label);
            const auto expected = std::string(label) == "Destination"
                                      ? path_utf8(std::filesystem::u8path(long_path) / "Game")
                                      : long_path;
            check(text(std::string("export:") + label) == expected,
                  "Export folder selection was truncated");
            type(std::string("export:") + label, long_path + "/typed-export");
        }
        const auto destination = root / "Output";
        const auto fallback = root / "FallbackKit";
        const auto request = exporter.request_from_selection(files.document, scene, destination,
                                                             fallback, {}, root / "runtime");
        check(request.destination == destination &&
                  request.runtime_kit == std::filesystem::u8path(long_path + "/typed-export"),
              "Export request lost the complete selected runtime-kit path");
        ui::GameExportTask untouched;
        check(untouched.request_from_selection(files.document, scene, destination, fallback, {}, {})
                      .runtime_kit == fallback,
              "Empty path fields no longer use the provided runtime-kit fallback");
        check(scene.document() == before && !scene.can_undo(),
              "Choosing permission folders changed authored state");
        check(!workspace.active() && !exporter.busy() && files.document.project() == root / "Game",
              "Choosing folders started a connection, export or project creation");
        std::cout
            << "Automation/project/export controls preserve long UTF-8 paths without mutation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
