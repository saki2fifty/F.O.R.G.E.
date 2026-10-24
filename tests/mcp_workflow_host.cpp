#include "automation.hpp"
#include "content.hpp"
#include "dependency_automation.hpp"
#include "export_automation.hpp"
#include "game_input.hpp"
#include "navigation_automation.hpp"
#include "play_automation.hpp"
#include "project_automation.hpp"
#include "project_source_automation.hpp"
#include "script_automation.hpp"
#include "ui_test_renderer.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Native owners with the existing recording RmlUi renderer; no GPU is simulated.
int main(int argc, char** argv) {
    try {
        if (argc != 6)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const auto runtime = std::filesystem::u8path(argv[2]);
        const auto kit = std::filesystem::u8path(argv[3]);
        const auto nav_worker = std::filesystem::u8path(argv[4]);
        const auto mode = std::string(argv[5]);
        forge::SceneDocument::create_project(root / "Game", "MCP game workflows");
        std::filesystem::create_directory(root / "Output");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        forge::ContentImports imports;
        imports.routes = [] { return std::vector<forge::AssetReimportRoute>{}; };
        forge::ui::GameExportTask export_task;
        forge::PlaySession play;
        forge::GameInput input;
        forge::RuntimeTuningReview tuning;
        forge::ProjectSettingsEditor settings;
        forge::NavigationTools navigation(engine.world(), nav_worker);
        forge::ContentBrowser content;
        content.refresh(files);
        forge::ui::RuntimeDependenciesEditor dependencies;
        auto tools = runtime.parent_path() / "forge_tools";
#ifdef _WIN32
        tools += ".exe";
#endif
        forge::ui::FlecsScriptEditor scripts(tools);
        forge::ui::Problems problems;
        UiTestRenderer renderer;
        std::unique_ptr<forge::UiPresenter> presenter;
        std::string presenter_session;
        std::uint64_t presenter_generation = 0, submitted = 0;
        const auto blocked = [&]() -> std::string {
            return play.active() || export_task.busy() || navigation.pending() || files.busy()
                       ? "Finish current native work"
                       : "";
        };
        forge::ui::AutomationWorkspace workspace;
        workspace.workflow_methods = [&](const auto& permissions) {
            auto methods =
                forge::ProjectAutomation::settings_methods(settings, files.document, permissions);
            const auto append = [&](auto values) {
                methods.insert(methods.end(), std::make_move_iterator(values.begin()),
                               std::make_move_iterator(values.end()));
            };
            append(forge::ProjectSourceAutomation::methods(files, permissions, blocked));
            append(forge::NavigationAutomation::methods(navigation, scene, files, permissions,
                                                        blocked));
            append(forge::DependencyAutomation::methods(
                dependencies, files, permissions, [&] { return content.catalog(); },
                [](forge::AssetId) {}, blocked));
            append(forge::ScriptAutomation::methods(scripts, files, permissions, blocked));
            forge::PlayAutomationProviders controls;
            controls.start = [&] {
                if (!blocked().empty() || settings.dirty())
                    throw std::runtime_error("Project work pending");
                play.configure(
                    files.document.settings().simulation_hz(), files.document.settings().input(),
                    files.document.settings().physics().gravity, files.document.project());
                play.set_headless_audio(true);
                play.start(forge::path_utf8(runtime), scene.snapshot());
            };
            controls.control_unavailable = [&] {
                return std::filesystem::exists(root / "gesture") ? "User gesture" : "";
            };
            controls.control = [&](const std::string& action) {
                if (!input.release_checked(play))
                    throw std::runtime_error("Input release failed");
                if (action == "stop")
                    play.stop();
                else if (action == "pause")
                    play.pause();
                else if (action == "resume")
                    play.resume();
                else if (action == "step")
                    play.step();
            };
            controls.prepare_input = [&] { return input.release_checked(play); };
            controls.select_entity = [&](const std::string& entity) {
                if (!scene.entity(entity).is_valid())
                    throw std::runtime_error("Choose a current scene entity");
            };
            controls.ui_command = [&](const std::string& instance, const std::string& name,
                                      const std::optional<std::string>& value) {
                return presenter && presenter->command(instance, name, value);
            };
            append(
                forge::PlayAutomation::methods(scene, files, play, tuning, controls, permissions));
            forge::ExportAutomationProviders exporting;
            exporting.request = [&](const auto& destination) {
                return forge::ui::GameExportTask::request_for(files.document, scene, destination,
                                                              kit, {}, runtime);
            };
            exporting.unavailable = [&]() -> std::string {
                const auto reason = blocked();
                return !reason.empty()                              ? reason
                       : files.document.dirty() || settings.dirty() ? "Save all drafts"
                                                                    : "";
            };
            append(forge::ExportAutomation::methods(files, export_task, exporting, permissions));
            return methods;
        };
        forge::ui::AutomationPermissions permissions;
        permissions.assets = mode != "read";
        permissions.execute = mode == "full" || mode == "execute";
        permissions.export_game = mode == "full" || mode == "output";
        if (permissions.export_game)
            permissions.export_root = root / "Output";
        workspace.start(scene, files, mode != "read", mode != "read", permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        std::string message;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(240);
        while (workspace.active() && std::chrono::steady_clock::now() < end) {
            files.changed = false;
            content.poll(files);
            if (!content.refreshing())
                content.refresh(files);
            dependencies.poll([&] { content.refresh(files); });
            scripts.poll(files.document, problems);
            imports.suspend(export_task.busy());
            imports.poll(files.document, message);
            export_task.poll(imports, [&] { imports.rescan(); });
            navigation.poll(scene, files.document, play.active(), message);
            play.pump();
            if (play.ready() && play.ui_snapshot().is_object()) {
                const auto& snapshot = play.ui_snapshot();
                const auto generation = snapshot.at("generation").get<std::uint64_t>();
                if (!presenter) {
                    presenter = std::make_unique<forge::UiPresenter>(
                        renderer, files.document.project(),
                        forge::asset_detail::read_bytes(kit / "resources/ui/LatoLatin-Regular.ttf",
                                                        4 * 1024 * 1024));
                }
                if (presenter_session != play.session() || presenter_generation != generation) {
                    presenter->reset(play.session(), generation);
                    presenter_session = play.session();
                    presenter_generation = generation;
                    submitted = 0;
                }
                presenter->accept(snapshot);
                if (!play.ui_ack().is_null())
                    presenter->acknowledge(play.ui_ack());
                if (const auto command = presenter->pending_command();
                    command && command->at("id") != submitted)
                    if (play.submit_ui(*command))
                        submitted = command->at("id").get<std::uint64_t>();
                presenter->update(.016, 800, 600);
                presenter->render();
            } else if (!play.active()) {
                presenter.reset();
                presenter_session.clear();
                presenter_generation = submitted = 0;
            }
            workspace.pump(files.document,
                           play.active() || export_task.busy() || navigation.pending()
                               ? "Native task active"
                               : "");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        presenter.reset();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
