#include "application_state_fixture.hpp"
#include "asset_automation.hpp"
#include "automation.hpp"
#include "content.hpp"
#include "dependency_automation.hpp"
#include "export_automation.hpp"
#include "flecs_system_trace.hpp"
#include "game_input.hpp"
#include "mcp_test_marker.hpp"
#include "navigation_automation.hpp"
#include "play_automation.hpp"
#include "project_automation.hpp"
#include "project_source_automation.hpp"
#include "script_automation.hpp"
#include "texture_authoring.hpp"
#include "tile_asset_reimport_routes.hpp"
#include "ui_localization_authoring.hpp"
#include "ui_test_renderer.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

// Native owners with the existing recording RmlUi renderer; no GPU is simulated.
int main(int argc, char** argv) {
    (void)forge::detail::initialize_native_system_tracing();
    try {
        if (argc != 6 && argc != 7 && argc != 8)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]);
        const auto runtime = std::filesystem::u8path(argv[2]);
        const auto kit = std::filesystem::u8path(argv[3]);
        const auto nav_worker = std::filesystem::u8path(argv[4]);
        const auto mode = std::string(argv[5]);
        struct SdlCleanup {
            bool initialized = false;
            ~SdlCleanup() {
                if (initialized)
                    SDL_Quit();
            }
        } sdl;
        if (mode == "gamepads") {
            if (!SDL_Init(SDL_INIT_GAMEPAD))
                throw std::runtime_error(SDL_GetError());
            sdl.initialized = true;
            SDL_VirtualJoystickDesc device;
            SDL_INIT_INTERFACE(&device);
            device.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            device.naxes = 6;
            device.nbuttons = 15;
            device.axis_mask = 63;
            device.button_mask = 32767;
            device.name = "FORGE MCP virtual gamepad";
            const std::array<SDL_VirtualJoystickSensorDesc, 2> sensors{
                {{SDL_SENSOR_ACCEL, 100}, {SDL_SENSOR_GYRO, 200}}};
            device.sensors = sensors.data();
            device.SetSensorsEnabled = [](void*, bool) { return true; };
            for (unsigned player = 0; player < 5; ++player) {
                device.nsensors = player == 4 ? 0 : 2;
                if (!SDL_AttachVirtualJoystick(&device))
                    throw std::runtime_error(SDL_GetError());
            }
        }
        const bool sdk_play = argc == 8 && std::string(argv[7]) == "--sdk";
        if (argc == 8 && !sdk_play)
            return 2;
        if (argc >= 7)
            std::filesystem::copy(std::filesystem::u8path(argv[6]), root / "Game",
                                  std::filesystem::copy_options::recursive);
        else
            forge::SceneDocument::create_project(root / "Game", "MCP game workflows");
        if (mode == "application_states")
            (void)forge::test::application_state_fixture(root / "Game");
        std::filesystem::create_directory(root / "Output");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        std::vector<std::string> recent;
        forge::EditorFiles files(scene, nullptr, recent);
        files.start(root / "Game");
        const std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> imgui(
            ImGui::CreateContext(), ImGui::DestroyContext);
        ImGui::GetIO().IniFilename = nullptr;
        forge::ContentImports imports;
        auto asset_worker = runtime.parent_path() / "forge_asset_build";
#ifdef _WIN32
        asset_worker += ".exe";
#endif
        imports.routes = [asset_worker, mode] {
            if (mode == "ui_localization")
                return std::vector<forge::AssetReimportRoute>{
                    {"forge.ui.localization", forge::desktop_texture_target(),
                     forge::ui_localization_import_registry(),
                     [](auto& candidate, const auto& plan, const auto&) {
                         forge::prepare_ui_localization_publication(candidate, plan);
                     }}};
            return forge::tile_asset_reimport_routes(asset_worker, forge::desktop_texture_target());
        };
        forge::SourceImport source;
        source.atlas_worker = asset_worker;
        source.routes = imports.routes;
        forge::ContentFiles content_files;
        forge::SceneAssetDrop placement;
        forge::ui::EditorSelection selection;
        files.external_busy = [&] {
            return source.busy() || content_files.busy() || placement.busy();
        };
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
            append(forge::asset_automation_methods(scene, files, imports, source, placement,
                                                   content_files, permissions));
            append(forge::NavigationAutomation::methods(navigation, scene, files, permissions,
                                                        blocked));
            append(forge::DependencyAutomation::methods(
                dependencies, files, permissions, [&] { return content.catalog(); },
                [](forge::AssetId) {}, blocked));
            append(forge::ScriptAutomation::methods(scripts, files, permissions, blocked));
            forge::PlayAutomationProviders controls;
            controls.start = [&] {
                if (!blocked().empty() || settings.dirty() || !imports.ready_for_play()) {
                    forge::Json detail{{"owner", blocked()},
                                       {"settings_dirty", settings.dirty()},
                                       {"imports_ready", imports.ready_for_play()},
                                       {"jobs", imports.jobs()}};
                    if (const auto sources = imports.sources()) {
                        detail["sources_complete"] = sources->complete;
                        detail["source_errors"] = forge::Json::array();
                        for (const auto& d : sources->diagnostics)
                            if (d.error)
                                detail["source_errors"].push_back(d.message);
                    }
                    throw forge::AuthoringError("play_busy",
                                                "Settle pending project work: " + detail.dump());
                }
                play.configure(files.document.settings().simulation_hz(),
                               files.document.settings().input(),
                               files.document.settings().physics().gravity,
                               files.document.project(), sdk_play, sdk_play);
                play.set_headless_audio(true);
                play.start(forge::path_utf8(runtime), scene.snapshot());
            };
            controls.control_unavailable = [&] {
                return forge::test::marker_present(root / "gesture") ? "User gesture" : "";
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
            controls.gamepads = [&] { return input.gamepads(); };
            controls.gamepad_motion = [&] { return input.gamepad_motion(play); };
            controls.configure_gamepad_sensor = [&](const forge::GamepadSensorConfig& value) {
                return input.configure_gamepad_sensor(play, value);
            };
            controls.assign_gamepad = [&](const forge::GamepadAssignment& value) {
                return input.assign_gamepad(play, value);
            };
            controls.select_entity = [&](const std::string& entity) {
                if (!scene.entity(entity).is_valid())
                    throw std::runtime_error("Choose a current scene entity");
            };
            controls.ui_command = [&](const std::string& instance, const std::string& name,
                                      const std::optional<std::string>& value) {
                return presenter && presenter->command(instance, name, value);
            };
            controls.ui_localization = [&] {
                return presenter ? presenter->localization_state() : forge::Json{};
            };
            controls.ui_application_states = [&] {
                return presenter ? presenter->application_states_state() : forge::Json{};
            };
            controls.reload_ui = [&] {
                if (!presenter)
                    throw forge::AuthoringError("ui_unavailable", "No live UI presenter");
                presenter->release_input();
                if (!presenter->reload())
                    throw forge::AuthoringError("ui_reload_failed", presenter->diagnostic());
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
        permissions.execute = mode == "full" || mode == "execute" || mode == "gamepads" ||
                              mode == "ui_localization" || mode == "application_states";
        permissions.export_game = mode == "full" || mode == "output" || mode == "gamepads" ||
                                  mode == "ui_localization" || mode == "application_states";
        if (permissions.export_game)
            permissions.export_root = root / "Output";
        workspace.start(scene, files, mode != "read", mode != "read", permissions);
        std::cout << workspace.connection().connection().dump() << std::endl;
        std::string message;
        const auto begin = std::chrono::steady_clock::now();
        const auto end = begin + std::chrono::seconds(240);
        while (workspace.active() && std::chrono::steady_clock::now() < end &&
               !forge::test::marker_present(root / "stop")) {
            files.changed = false;
            content.poll(files);
            if (!content.refreshing())
                content.refresh(files);
            dependencies.poll(files.document, [&] { content.refresh(files); });
            scripts.poll(files.document, problems);
            placement.poll(scene, files, selection, play.active() || export_task.busy(), message);
            content_files.poll(files.document, scene, imports, message);
            source.poll(files.document, imports, message);
            imports.suspend(export_task.busy() || source.busy() || content_files.busy());
            imports.poll(files.document, message);
            export_task.poll(imports, [&] { imports.rescan(); });
            navigation.poll(scene, files.document, play.active(), message);
            play.pump();
            if (sdk_play && !play.sdk_candidate_envelope().is_null())
                play.submit_sdk_candidate_ack(true, play.session(), play.sdk_candidate_ticket());
            if (sdk_play || mode == "gamepads")
                input.pump(play, true);
            if (play.ready() && play.ui_snapshot().is_object() &&
                (presenter || (play.ui_snapshot().contains("documents") &&
                               !play.ui_snapshot().at("documents").empty()))) {
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
                presenter->update(
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(),
                    800, 600);
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
