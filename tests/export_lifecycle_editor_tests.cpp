#include "editor/export_automation.hpp"
#include "editor_state.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool okay, const char* message) {
    if (!okay)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need export lifecycle scratch directory");
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
        ui::GameExportTask task;
        ContentImports imports;
        files.external_busy = [&] { return task.busy(); };
        ImGui::CreateContext();
        struct UiCleanup {
            ~UiCleanup() {
                test::observe_ui = false;
                ImGui::DestroyContext();
            }
        } ui_cleanup;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1100, 800};
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
            files.changed = false; // The main loop consumes project changes each frame.
            files.pump(!task.busy());
            task.poll(imports, [] {});
            test::ui_targets.clear();
            ImGui::NewFrame();
            task.draw(nullptr, files.document, scene, false, false, [] {});
            ImGui::Render();
        };
        auto close = [&] {
            const auto box = test::ui_targets.at("button:Close Export");
            io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                (box.minimum.y + box.maximum.y) * .5f);
            frame();
            io.AddMouseButtonEvent(0, true);
            frame();
            io.AddMouseButtonEvent(0, false);
            frame();
            frame();
            check(!test::ui_targets.contains("export:start"), "Close Export did not hide its UI");
        };
        task.open();
        frame();
        frame();
        const auto first_scene = scene.document();
        const auto request = ui::GameExportTask::request_for(files.document, scene, root / "Output",
                                                             root / "MissingKit", {}, {});
        task.begin(files.document, request);
        check(task.busy() && files.busy(), "Queued export did not hold the project-switch guard");
        bool rejected = false;
        try {
            files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && files.document.project() == first,
              "Project switch bypassed queued export work");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            frame();
            check(std::chrono::steady_clock::now() < deadline, "Export worker did not settle");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (task.busy());
        check(!task.error().empty() && task.output().empty(),
              "The real exporter did not reject the unconfigured project/missing kit");
        const auto error = task.error();
        check(ExportAutomation::state(task, files).at("error") == error,
              "Automation and native export owners disagreed on failure");
        frame();
        check(task.error() == error && scene.document() == first_scene && !scene.can_undo(),
              "Same-project polling discarded the result or changed authored state");
        close();
        frame();
        check(task.error() == error, "Closing the export window discarded the current result");
        files.automation_project({EditorFiles::Command::OpenProject, second, {}}, false);
        frame(); // The editor draws the owner every frame, including while closed.
        check(!ExportAutomation::state(task, files).at("started").get<bool>(),
              "Automation adopted the previous project's operation");
        check(task.error().empty() && task.output().empty(),
              "Previous project's export error remained in the current project");
        task.open();
        frame();
        check(task.error().empty() && test::ui_targets.contains("export:start"),
              "Reopening export revived a previous-project result");
        close();
        files.automation_project({EditorFiles::Command::OpenProject, first, {}}, false);
        task.begin(files.document, request);
        const auto next_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (task.busy()) {
            task.poll(imports, [] {}); // An agent can finish work before the panel next draws.
            check(std::chrono::steady_clock::now() < next_deadline,
                  "Second export worker did not settle");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto current_error = task.error();
        check(!current_error.empty(), "Second export did not produce a current-project result");
        frame();
        check(task.error() == current_error &&
                  ExportAutomation::state(task, files).at("error") == current_error,
              "A matching operation completed before drawing was discarded");
        check(!std::filesystem::exists(root / "Output") && !scene.can_undo(),
              "Rejected export or project observation published output/authored history");
        std::cout << "Export result scope follows the project while existing busy guards remain\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
