#ifdef FORGE_UI_FIXTURE
#include "editor_fixture.hpp"
#endif
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "ImGuiImplSDL3.hpp"
#include "actions.hpp"
#include "animation_debug.hpp"
#include "animation_tools.hpp"
#include "automation.hpp"
#include "blockout.hpp"
#include "camera_controls.hpp"
#include "command_workspace.hpp"
#include "component_inspector.hpp"
#include "content.hpp"
#include "files.hpp"
#include "game_input.hpp"
#include "help.hpp"
#include "hierarchy.hpp"
#include "native_build.hpp"
#include "navigation_tools.hpp"
#include "orientation.hpp"
#include "performance.hpp"
#include "play.hpp"
#include "prefabs.hpp"
#include "project_settings.hpp"
#include "runtime_ui_host.hpp"
#include "runtime_ui_tools.hpp"
#include "scene_cache.hpp"
#include "scene_tools.hpp"
#include "status_bar.hpp"
#include "transform_gesture.hpp"
#include "view_state.hpp"
#include "viewport.hpp"
#include "widgets.hpp"
#include "workspace.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <forge/build.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <memory>
using namespace Diligent;
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "FORGE editor | Build: " << forge::build_id << '\n';
        return 0;
    }
    std::clog << "FORGE editor | Build: " << forge::build_id << '\n';
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::cerr << SDL_GetError();
        return 1;
    }
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("F.O.R.G.E. | Native ECS Editor", 1440, 900,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow);
    if (!window) {
        std::cerr << SDL_GetError();
        SDL_Quit();
        return 1;
    }
    int result = 0;
    try {
#ifdef FORGE_UI_FIXTURE
        forge::test::EditorFixture fixture(argc, argv);
#endif
        auto* factory = LoadAndGetEngineFactoryD3D12();
        if (!factory)
            throw std::runtime_error("D3D12 backend unavailable");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        RefCntAutoPtr<ISwapChain> swap;
        EngineD3D12CreateInfo engine;
#ifdef FORGE_UI_FIXTURE
        fixture.device(factory, &device, &context);
#else
        factory->CreateDeviceAndContextsD3D12(engine, &device, &context);
#endif
        if (!device || !context)
            throw std::runtime_error("D3D12 device initialization failed");
        auto hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window.get()),
                                           SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        SwapChainDesc desc;
        desc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
        desc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;
        factory->CreateSwapChainD3D12(device, context, desc, FullScreenModeDesc{},
                                      Win32NativeWindow{hwnd}, &swap);
        if (!swap)
            throw std::runtime_error("D3D12 swap chain initialization failed");
        std::string ini;
        auto gui =
            ImGuiImplSDL3::Create(ImGuiDiligentCreateInfo{device, swap->GetDesc()}, window.get());
        forge::ui::style();
        // Reuse the already packaged OFL-licensed runtime UI font; no new dependency.
        const auto editor_font =
            std::filesystem::path(SDL_GetBasePath()) / "resources/ui/LatoLatin-Regular.ttf";
        if (std::filesystem::exists(editor_font))
            ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->AddFontFromFileTTF(
                forge::path_utf8(editor_font).c_str(), 16.f);
        else
            ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->AddFontDefaultVector();
        auto* log_font = ImGui::GetIO().Fonts->AddFontDefaultBitmap();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        char* preferences = SDL_GetPrefPath("FORGE", "Editor");
        if (!preferences)
            throw std::runtime_error("Cannot locate editor preferences directory");
#ifdef FORGE_UI_FIXTURE
        const std::filesystem::path config = fixture.config;
#else
        const std::filesystem::path config = preferences;
#endif
        SDL_free(preferences);
        ini = (config / "workspace.ini").string();
        const auto startup_layout = forge::ui::prepare_layout(config / "workspace.ini");
        forge::ui::load_startup_layout(startup_layout, ini.c_str());
        if (!startup_layout.warning.empty())
            std::clog << startup_layout.warning << '\n';
        const auto settings = config / "settings.json";
        char cmake_path[1024] = "cmake";
        char ninja_path[1024] = "ninja";
        bool auto_build = false;
        forge::ui::SceneTools scene_tools;
        forge::ui::ModalTransform modal;
        forge::ui::OrientationGizmo orientation;
        forge::ui::Workspace workspace;
        forge::ui::EditorUiContext editor;
        forge::ui::ContextScope editor_scope(editor);
        forge::ComponentInspector component_inspector;
        forge::ContentBrowser content;
        forge::BlockoutProperties blockout;
        forge::ui::CommandWorkspace commands;
        char hierarchy_filter[256]{};
        std::vector<std::string> recent_projects;
        std::string last_project;
        try {
            std::ifstream f(settings);
            if (f) {
                forge::Json j;
                f >> j;
                forge::ui::tooltips = j.value("tooltips", true);
                forge::ui::style(j.value("interface_scale", 1.0f));
                SDL_strlcpy(cmake_path, j.value("cmake", std::string("cmake")).c_str(),
                            sizeof(cmake_path));
                SDL_strlcpy(ninja_path, j.value("ninja", std::string("ninja")).c_str(),
                            sizeof(ninja_path));
                workspace.load(j);
                orientation.visible = j.value("orientation_gizmo", true);
                auto_build = j.value("auto_build", false);
                blockout.at_view_target = j.value("create_at_view_target", false);
                scene_tools.grid = j.value("grid", true);
                scene_tools.move_tool = j.value("scene_tool", std::string("move")) != "select";
                scene_tools.snap = j.value("snap", false);
                scene_tools.snap_step = std::clamp(j.value("snap_step", 1.0f), 0.01f, 1000.0f);
                scene_tools.grid_step = std::clamp(j.value("grid_step", 1.0f), 0.1f, 1000.0f);
                scene_tools.fly_speed = std::clamp(j.value("fly_speed", 5.0f), 0.1f, 1000.0f);
                recent_projects = j.value("recent_projects", std::vector<std::string>{});
                last_project = j.value("last_project", std::string{});
            }
        } catch (const std::exception&) { /* Recover malformed user preferences with defaults. */
        }
        auto save_preferences = [&] {
            forge::atomic_write(
                settings, forge::Json{{"tooltips", forge::ui::tooltips},
                                      {"panels", workspace.settings()},
                                      {"orientation_gizmo", orientation.visible},
                                      {"interface_scale", forge::ui::interface_scale},
                                      {"cmake", cmake_path},
                                      {"ninja", ninja_path},
                                      {"auto_build", auto_build},
                                      {"create_at_view_target", blockout.at_view_target},
                                      {"grid", scene_tools.grid},
                                      {"scene_tool", scene_tools.move_tool ? "move" : "select"},
                                      {"snap", scene_tools.snap},
                                      {"snap_step", scene_tools.snap_step},
                                      {"grid_step", scene_tools.grid_step},
                                      {"fly_speed", scene_tools.fly_speed},
                                      {"recent_projects", recent_projects},
                                      {"last_project", last_project}}
                              .dump(2));
        };
        forge::EngineContext scene_engine;
        forge::Scene scene(scene_engine.world());
        editor.scene = &scene;
        forge::ui::AutomationWorkspace automation;
        forge::PlaySession play;
        forge::GameInput game_input;
        forge::ProjectSettingsEditor project_settings;
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("Cannot locate runtime directory");
        const auto runtime_path = (std::filesystem::path(base) / "forge_runtime.exe").string();
        forge::EditorFiles files(scene, window.get(), recent_projects);
        forge::NavigationTools navigation_tools(std::filesystem::path(base) /
                                                "forge_nav_build.exe");
        forge::AnimationTools animation_tools(std::filesystem::path(base) / "tools/gltf2ozz.exe");
        std::string message = "Ready. Use Entity or Scene > Create to add an object.";
        auto perform = [&](auto&& action) {
            try {
                action();
            } catch (const std::exception& e) {
                message = e.what();
                forge::Diagnostic diagnostic{forge::Severity::Error, "editor", message, {}};
                diagnostic.context.asset = scene.asset_id();
                scene_engine.services().emit(std::move(diagnostic));
                forge::ui::report_error("editor", message);
            }
        };
        auto open_scratch = [&] {
            const auto scratch = config / "Projects" / "Scratch";
            if (!std::filesystem::exists(scratch)) {
                std::filesystem::create_directories(scratch.parent_path());
                forge::SceneDocument::create_project(scratch, "Scratch");
            }
            files.start(scratch);
        };
        try {
#ifdef FORGE_UI_FIXTURE
            files.start(fixture.project);
#else
            if (argc > 1)
                files.start(std::filesystem::u8path(argv[1]));
            else if (!last_project.empty())
                files.start(std::filesystem::u8path(last_project));
            else {
                open_scratch();
                message = "Scratch project opened. Use File > New project or Open project for your "
                          "own game.";
            }
#endif
        } catch (const std::exception& e) {
            if (argc > 1)
                throw;
            message = std::string(e.what()) + ". Opening the Scratch project.";
            open_scratch();
        }
        if (!startup_layout.warning.empty())
            message = startup_layout.warning;
        auto native = std::make_unique<forge::NativeBuild>(
            files.document.project(), std::filesystem::path(base) / "sdk", runtime_path);
        native->cmake = cmake_path;
        native->ninja = ninja_path;
        native->auto_build = auto_build;
        auto active_project = files.document.project();
        bool initialize_layout = startup_layout.text.empty();
        forge::Viewport viewport(device), game_viewport(device);
        forge::RuntimeUiHost runtime_ui(window.get(), device,
                                        std::filesystem::path(base) /
                                            "resources/ui/LatoLatin-Regular.ttf");
        forge::RuntimeUiTools runtime_ui_tools;
        forge::AuthoringSnapshot authoring_snapshot;
        forge::PrefabEditor prefab_editor;
        forge::PreviewSnapshot preview_snapshot, game_preview_snapshot;
        forge::EditorCamera game_camera;
        bool game_visible = false, focus_game = false;
        forge::EditorCamera camera;
        try {
            forge::restore_view(files.document, camera);
        } catch (const std::exception& e) {
            message = e.what();
        }
        forge::Telemetry telemetry;
        forge::ui::Performance performance;
        auto& selected = editor.selection.entity_slot();
        std::string name_entity, authored_name;
        std::optional<forge::EditorFiles::Action> pending_switch;
        files.before_request = [&](const forge::EditorFiles::Action& action) {
            if (!prefab_editor.dirty() && !project_settings.dirty())
                return true;
            pending_switch = action;
            if (prefab_editor.dirty())
                prefab_editor.request_close();
            else
                project_settings.request_close();
            return false;
        };
        files.save_active = [&] {
            if (editor.task.owner == forge::ui::DocumentTask::Prefab && prefab_editor.is_open())
                prefab_editor.request_save();
            else if (editor.task.owner == forge::ui::DocumentTask::Settings &&
                     project_settings.is_open())
                project_settings.request_save();
            else
                files.save();
        };
        char entity_name[1024]{};
        bool running = true;
        std::string current_title;
#ifdef FORGE_UI_FIXTURE
        editor.selection.select_entity(
            forge::authoring_command(scene, "entity.create", {{"name", "Fixture cube"}})
                .at("selected"));
        forge::authoring_command(scene, "component.add",
                                 {{"entity", selected}, {"component", "forge.ui_document"}});
        forge::authoring_command(scene, "component.add",
                                 {{"entity", selected}, {"component", "forge.audio_source"}});
        fixture.prefab = files.document.prefabs().create(
            scene, forge::create_prefab_source(scene, selected), "Assets/Fixture.prefab.json");
        files.document.save();
#endif
        while (running) {
            performance.begin();
            SDL_Event event;
            game_input.pump(play,
                            workspace.game && game_visible && !files.busy() &&
                                !ImGui::GetIO().WantTextInput &&
                                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                                 ImGuiPopupFlags_AnyPopupLevel) &&
                                (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS));
            while (SDL_PollEvent(&event)) {
                const bool zoom_key =
                    (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                    (event.key.mod & SDL_KMOD_CTRL) &&
                    (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS ||
                     event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS ||
                     event.key.key == SDLK_KP_PLUS || event.key.key == SDLK_0 ||
                     event.key.key == SDLK_KP_0);
                if (zoom_key || (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                                 game_input.outside(event.button.x, event.button.y)))
                    game_input.release(play);
                if (runtime_ui.event(event, game_input, play))
                    continue;
                if (game_input.event(event, play))
                    continue;
                gui->HandleSDLEvent(&event);
                if (event.type == SDL_EVENT_KEY_DOWN && (event.key.mod & SDL_KMOD_CTRL)) {
                    float scale = forge::ui::interface_scale;
                    if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS)
                        scale -= 0.1f;
                    else if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS ||
                             event.key.key == SDLK_KP_PLUS)
                        scale += 0.1f;
                    else if (event.key.key == SDLK_0 || event.key.key == SDLK_KP_0)
                        scale = 1.0f;
                    if (scale != forge::ui::interface_scale) {
                        forge::ui::style(scale);
                        try {
                            perform(save_preferences);
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                    }
                }
                if (event.type == SDL_EVENT_QUIT)
                    files.request({forge::EditorFiles::Command::Quit, {}, {}});
            }
            files.pump(!native->busy());
            if (files.changed) {
                play.stop();
                editor.selection.clear();
                camera = {};
                scene_tools.move.cancel();
                modal.cancel();
                blockout.cancel();
                try {
                    forge::restore_view(files.document, camera);
                } catch (const std::exception& e) {
                    message = e.what();
                }
                if (active_project != files.document.project()) {
                    active_project = files.document.project();
                    editor.problems.reset();
                    editor.log.clear();
                    editor.last_status.clear();
                    native = std::make_unique<forge::NativeBuild>(
                        active_project, std::filesystem::path(base) / "sdk", runtime_path);
                    native->cmake = cmake_path;
                    native->ninja = ninja_path;
                    native->auto_build = auto_build;
                }
                files.changed = false;
            }
            if (files.preferences_changed) {
                last_project = forge::path_text(files.document.project());
                try {
                    perform(save_preferences);
                } catch (const std::exception& e) {
                    message = e.what();
                }
                files.preferences_changed = false;
            }
            if (files.quit) {
                running = false;
                continue;
            }
            play.pump();
            runtime_ui.sync(play, files.document.project(), workspace.game);
            native->simulation_hz = files.document.settings().simulation_hz();
            native->gravity = files.document.settings().physics().gravity;
            native->pump(play, authoring_snapshot.snapshot(scene));
            files.set_switch_available(!native->busy());
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window.get(), &width, &height);
            if (width <= 0 || height <= 0 ||
                SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) {
                telemetry.pause();
                SDL_Delay(20);
                continue;
            }
            if (swap->GetDesc().Width != unsigned(width) ||
                swap->GetDesc().Height != unsigned(height))
                swap->Resize(width, height);
            const std::string automation_busy =
                play.active()                     ? "Stop play before editing"
                : native->busy()                  ? "Wait for the native build"
                : (files.busy() || files.changed) ? "Finish the file operation"
                : (scene_tools.move.active() || modal.active() || blockout.active() ||
                   ImGui::IsAnyItemActive() ||
                   ImGui::IsPopupOpen(nullptr,
                                      ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
                    ? "Finish the active UI interaction"
                    : "";
            automation.pump(files.document, automation_busy);
            gui->NewFrame(width, height, swap->GetDesc().PreTransform);
            // Preserve live numeric edits; ImGui 1.92.9 changed its default to commit-on-exit.
            ImGui::PushItemFlag(ImGuiItemFlags_LiveEditOnInputScalar, true);
            telemetry.frame();
            forge::ui::status_bar(telemetry, play.active(), scene.entity_count(),
                                  play.can_recover() ? "CRASHED"
                                  : play.active()    ? (play.paused() ? "PAUSED" : "PLAYING")
                                                     : "EDIT",
                                  editor.problems.size(), !selected.empty(), native->busy());
            const auto dock = ImGui::DockSpaceOverViewport();
            if (initialize_layout || workspace.reset) {
                workspace.reset = false;
                forge::ui::initialize_workspace(dock);
                content.focus();
                initialize_layout = false;
            }
            const bool edit_locked = play.active() || native->busy() || files.busy() ||
                                     scene_tools.move.active() || modal.active() ||
                                     blockout.active();
            editor.selection.reconcile(scene.document());
            if ((editor.task.owner == forge::ui::DocumentTask::Prefab &&
                 !prefab_editor.is_open()) ||
                (editor.task.owner == forge::ui::DocumentTask::Settings &&
                 !project_settings.is_open()))
                editor.task.owner = forge::ui::DocumentTask::Scene;
            const bool scene_task = editor.task.owner == forge::ui::DocumentTask::Scene;
            forge::ui::EditorActions actions;
            auto add_action = [&](std::string id, std::string label, std::string shortcut,
                                  std::string help, bool enabled, std::function<void()> run) {
                actions.entries.push_back({std::move(id), std::move(label), std::move(shortcut),
                                           std::move(help), enabled, [&, run] { perform(run); }});
            };
            add_action("save", std::string("Save / ") + editor.task.name(), "Ctrl+S",
                       "Save or Publish the active task. Scene, prefab and settings have "
                       "independent ownership.",
                       !edit_locked, [&] { files.save_active(); });
            add_action(
                "undo", "Undo scene", "Ctrl+Z",
                "Undo one scene edit. Unavailable when an independent draft owns the active task.",
                !edit_locked && scene_task && scene.can_undo(),
                [&] { forge::authoring_history(scene, false); });
            add_action("redo", "Redo scene", "Ctrl+Y",
                       "Redo one scene edit. Ctrl+Shift+Z also works.",
                       !edit_locked && scene_task && scene.can_redo(),
                       [&] { forge::authoring_history(scene, true); });
            for (auto entry : forge::ui::palette_entries(selected, scene_tools.snap_step,
                                                         camera.target, blockout.at_view_target)) {
                if (entry.operation == "history.undo" || entry.operation == "history.redo")
                    continue;
                const bool inspect =
                    entry.operation == "diagnostics" || entry.operation == "schema";
                std::string shortcut = entry.operation == "entity.duplicate" ? "Ctrl+D"
                                       : entry.operation == "entity.delete"  ? "Delete"
                                                                             : "";
                add_action(entry.label, entry.label, shortcut, entry.help,
                           entry.available && (inspect || (!edit_locked && scene_task)),
                           [&, entry] {
                               if (entry.operation == "diagnostics")
                                   commands.diagnostics_open = true;
                               else if (entry.operation == "schema")
                                   commands.schema_open = true;
                               else {
                                   auto arguments = entry.arguments;
                                   if (arguments.contains("entity"))
                                       arguments["entity"] = selected;
                                   editor.selection.select_entity(
                                       forge::authoring_command(scene, entry.operation, arguments)
                                           .at("selected")
                                           .get<std::string>());
                               }
                           });
            }
            add_action("rename", "Rename entity", "F2",
                       "Focus the selected entity name in Inspector.",
                       !selected.empty() && !edit_locked && scene_task, [&] {
                           editor.rename_entity = true;
                           workspace.inspector = true;
                       });
            add_action("add_component", "Add Component", "",
                       "Search registered components for the selected entity.",
                       !selected.empty() && !edit_locked && scene_task, [&] {
                           editor.add_component = true;
                           workspace.inspector = true;
                       });
            add_action("play", "Play", "",
                       "Start the isolated runtime and show Game. Authoring remains in Scene.",
                       !play.active() && !native->busy() && !files.busy() && !modal.active() &&
                           !blockout.active() && !scene_tools.move.active(),
                       [&] {
                           if (files.document.settings().requires_native_sdk())
                               throw std::runtime_error(
                                   "This project requires the exact-version SDK runtime. Use "
                                   "forge_runtime --sdk-project; editor Play supports ABI1.");
                           game_camera = camera;
                           play.configure(files.document.settings().simulation_hz(),
                                          files.document.settings().input(),
                                          files.document.settings().physics().gravity,
                                          files.document.project());
                           play.start(runtime_path, scene.snapshot(), native->artifact());
                           workspace.game = true;
                           focus_game = true;
                       });
            add_action("pause", play.active() && play.paused() ? "Resume" : "Pause", "F6",
                       "Pause or resume the runtime clock.", play.control_ready(), [&] {
                           if (play.paused())
                               play.resume();
                           else
                               play.pause();
                       });
            add_action("step", "Step", "F7", "Run exactly one fixed tick and remain paused.",
                       play.control_ready() && play.paused(), [&] { play.step(); });
            add_action("stop", "Stop", "",
                       "Stop the isolated runtime. The authored scene is preserved.", play.active(),
                       [&] {
                           game_input.release(play);
                           play.stop();
                       });
            add_action("recover", "Recover runtime", "",
                       "Reconstruct the last compatible completed checkpoint.",
                       play.can_recover() && !native->busy() && !files.busy(), [&] {
                           play.recover();
                           workspace.game = true;
                           focus_game = true;
                       });
            commands.actions = &actions;
#ifdef FORGE_UI_FIXTURE
            if (SDL_GetTicks() - fixture.started > 110000)
                throw std::runtime_error("Editor fixture timed out");
            if (!fixture.prepared) {
                bool ready = true;
                switch (fixture.stage) {
                case 0:
                    break;
                case 1:
                    editor.add_component = true;
                    break;
                case 2:
                    ImGui::ClosePopupToLevel(0, true);
                    ImGui::SetWindowFocus("Content");
                    break;
                case 3:
                    try {
                        forge::authoring_command(scene, "property.set",
                                                 {{"entity", selected},
                                                  {"component", "forge.ui_document"},
                                                  {"field", "layer"},
                                                  {"value", -1}});
                    } catch (const std::exception& e) {
                        editor.problems.report({"fixture-invalid-layer",
                                                "Error",
                                                e.what(),
                                                selected,
                                                {},
                                                "UI Document / Layer",
                                                scene.asset_id()});
                    }
                    ImGui::SetWindowFocus("Problems###Problems");
                    break;
                case 4:
                    prefab_editor.edit_source(files.document, fixture.prefab);
                    prefab_editor.rename_member("Draft name");
                    break;
                case 5:
                    prefab_editor.resolve_close(forge::ui::DraftResolution::Discard, scene,
                                                files.document);
                    project_settings.open();
                    break;
                case 6:
                    project_settings.request_close();
                    actions.invoke("play");
                    break;
                case 7:
                    if (play.control_ready()) {
                        if (!play.paused())
                            play.pause();
                        game_input.capture(play);
                    } else
                        ready = false;
                    break;
                case 8:
                    editor.selection.select_entity(
                        scene.document().at("entities")[0].at("id").get<std::string>());
                    game_input.release(play);
                    play.stop();
                    SDL_SetWindowSize(window.get(), 960, 640);
                    ImGui::SetWindowFocus("Scene");
                    break;
                case 9:
                    forge::ui::style(2);
                    break;
                case 10:
                    SDL_SetWindowSize(window.get(), 1920, 1080);
                    workspace.reset = true;
                    break;
                case 11:
                    forge::ui::style(1);
                    SDL_SetWindowSize(window.get(), 2560, 1080);
                    break;
                }
                fixture.prepared = ready;
            }
#endif
            const auto panels_before = workspace.settings();
            if (forge::ui::begin_toolbar()) {
                const bool narrow_menu =
                    ImGui::GetMainViewport()->WorkSize.x < 900 * forge::ui::interface_scale;
                const bool menu_open = !narrow_menu || ImGui::BeginMenu("Menu");
                if (menu_open) {
                    files.menu([&] { actions.item("save"); }, scene_task && !edit_locked);
                    if (ImGui::BeginMenu("Edit")) {
                        actions.item("save");
                        actions.item("undo");
                        actions.item("redo");
                        ImGui::Separator();
                        actions.item("rename");
                        actions.item("add_component");
                        if (ImGui::MenuItem("Command palette", "Ctrl+Shift+P"))
                            commands.open_palette();
                        forge::ui::help("Search all currently registered editor actions.");
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Entity")) {
                        for (unsigned kind = 0; kind < 4; ++kind)
                            actions.item(std::string("Create / ") + forge::primitive_names[kind]);
                        ImGui::Separator();
                        actions.item("Entity / Duplicate subtree", "Duplicate subtree");
                        actions.item("Entity / Delete subtree", "Delete subtree");
                        actions.item("Entity / Move to scene root", "Move to scene root");
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Run")) {
                        for (auto id : {"play", "pause", "step", "stop", "recover"})
                            actions.item(id);
                        ImGui::EndMenu();
                    }
                    if (workspace.menu())
                        perform(save_preferences);
                    commands.menu([&] {
                        automation.menu();
                        performance.menu();
                        project_settings.menu();
                    });
                    forge::ui::help_menu(std::filesystem::path(base), message);
                    if (ImGui::BeginMenu("Preferences")) {
                        if (ImGui::Checkbox("Tooltips", &forge::ui::tooltips))
                            perform(save_preferences);
                        forge::ui::help("Persistent contextual help. Interface zoom is "
                                        "Ctrl+Minus/Plus and Ctrl+0.");
                        ImGui::Text("UI scale %.0f%%", forge::ui::interface_scale * 100);
                        if (ImGui::MenuItem("Reset UI scale", "Ctrl+0")) {
                            forge::ui::style(1);
                            perform(save_preferences);
                        }
                        ImGui::EndMenu();
                    }
                    if (narrow_menu)
                        ImGui::EndMenu();
                }
                forge::ui::end_toolbar();
            }
            const auto* main_viewport = ImGui::GetMainViewport();
            const bool compact_toolbar =
                main_viewport->WorkSize.x < 850 * forge::ui::interface_scale;
            const float toolbar_height =
                ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().WindowPadding.y;
            if (ImGui::BeginViewportSideBar(
                    "##global-actions", ImGui::GetMainViewport(), ImGuiDir_Up, toolbar_height,
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoFocusOnAppearing)) {
                actions.button("save", "Save");
                if (!compact_toolbar) {
                    ImGui::SameLine();
                    actions.button("undo", "Undo");
                    ImGui::SameLine();
                    actions.button("redo", "Redo");
                }
                ImGui::SameLine();
                actions.button("play");
                if (!compact_toolbar) {
                    ImGui::SameLine();
                    actions.button("pause");
                    ImGui::SameLine();
                    actions.button("step");
                }
                ImGui::SameLine();
                actions.button("stop");
                ImGui::SameLine();
                if (ImGui::Button("More..."))
                    ImGui::OpenPopup("Global actions");
                forge::ui::help("All global actions, including controls hidden at narrow widths.");
                if (ImGui::BeginPopup("Global actions")) {
                    for (auto id :
                         {"save", "undo", "redo", "play", "pause", "step", "stop", "recover"})
                        actions.item(id);
                    ImGui::EndPopup();
                }
                if (!compact_toolbar) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("Active: %s", editor.task.name());
                    forge::ui::help("Save follows this active task. Scene Undo is unavailable for "
                                    "independent drafts.");
                }
            }
            ImGui::End();
            if (!edit_locked)
                files.shortcuts(scene_task);
            if (!edit_locked &&
                !ImGui::IsPopupOpen(nullptr,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                    actions.invoke(ImGui::GetIO().KeyShift ? "redo" : "undo");
                if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                    actions.invoke("redo");
                if (ImGui::IsKeyPressed(ImGuiKey_D, false))
                    actions.invoke("Entity / Duplicate subtree");
            }
            if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                !ImGui::IsPopupOpen(nullptr,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                if (ImGui::IsKeyPressed(ImGuiKey_F6, false))
                    actions.invoke("pause");
                if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
                    actions.invoke("step");
            }
            commands.draw(scene, selected, message, scene_tools.snap_step, camera.target,
                          blockout.at_view_target, edit_locked);
            files.draw_dialogs();
            animation_tools.poll(files.document, message);
            navigation_tools.poll(scene, files.document, play.active(), message);
            automation.draw(scene, files.document, automation_busy);
            const auto title = std::string(files.document.dirty() ? "* " : "") +
                               files.document.name() + " / " +
                               (files.document.path().empty()
                                    ? "Untitled"
                                    : forge::path_text(files.document.path().filename())) +
                               " | F.O.R.G.E. | Build: " + forge::build_id;
            if (title != current_title) {
                SDL_SetWindowTitle(window.get(), title.c_str());
                current_title = title;
            }
            if (scene_tools.move.active() &&
                (!scene_tools.move.valid_for(scene) ||
                 !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) || play.active() ||
                 files.busy()))
                scene_tools.move.cancel();
            if (modal.active() && (!modal.gesture.valid(scene, selected) ||
                                   !(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) ||
                                   play.active() || files.busy()))
                modal.cancel();
            blockout.check(scene);
            if (!(SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) || files.busy())
                blockout.cancel();
            const auto& doc = authoring_snapshot.document(scene);
            if (workspace.hierarchy) {
                if (ImGui::Begin("Hierarchy###World", &workspace.hierarchy)) {
                    ImGui::TextUnformatted(files.document.dirty()     ? "Unsaved changes"
                                           : files.document.on_disk() ? "Saved"
                                                                      : "New scene");
                    forge::ui::help("Save writes the current scene; dirty scenes get a recovery "
                                    "snapshot every 30 seconds.");
                    ImGui::InputTextWithHint("##entity-filter", "Search names or IDs...",
                                             hierarchy_filter, sizeof(hierarchy_filter));
                    forge::ui::help(
                        "Filter entity names and IDs; matching descendants retain their "
                        "ancestors. ASCII case-insensitive.");
                    int expand = 0;
                    if (forge::ui::button("Expand all", "Expand all hierarchy branches."))
                        expand = 1;
                    ImGui::SameLine();
                    if (forge::ui::button(
                            "Collapse all",
                            "Collapse all hierarchy branches. Search keeps matching paths open."))
                        expand = -1;
                    editor.task.focus(forge::ui::DocumentTask::Scene);
                    forge::ui::hierarchy(doc, selected, hierarchy_filter, expand, &scene,
                                         edit_locked);
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                        !selected.empty())
                        ImGui::OpenPopup("Entity actions");
                    if (ImGui::BeginPopup("Entity actions")) {
                        actions.item("rename");
                        actions.item("Entity / Duplicate subtree", "Duplicate subtree");
                        actions.item("Entity / Delete subtree", "Delete subtree");
                        actions.item("add_component");
                        actions.item("Entity / Move to scene root", "Move to scene root");
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2, false))
                        actions.invoke("rename");
                    if (ImGui::IsWindowFocused() && !edit_locked && !ImGui::GetIO().WantTextInput &&
                        !ImGui::IsAnyItemActive() && !selected.empty() &&
                        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                        actions.invoke("Entity / Delete subtree");
                    }
                }
                ImGui::End();
            }
            if (workspace.inspector) {
                if (ImGui::Begin("Inspector", &workspace.inspector)) {
                    ImGui::BeginDisabled(play.active() || native->busy() || files.busy() ||
                                         scene_tools.move.active() || modal.active());
                    editor.task.focus(editor.selection.kind() ==
                                                  forge::ui::SelectionKind::PrefabMember &&
                                              prefab_editor.is_open()
                                          ? forge::ui::DocumentTask::Prefab
                                          : forge::ui::DocumentTask::Scene);
                    if (editor.selection.kind() == forge::ui::SelectionKind::Asset ||
                        editor.selection.kind() == forge::ui::SelectionKind::PrefabMember) {
                        if (editor.selection.kind() == forge::ui::SelectionKind::PrefabMember) {
                            ImGui::TextWrapped("Prefab member selected. Edit its source in the "
                                               "Prefab source window.");
                            forge::ui::help("This selection belongs to the independent prefab "
                                            "draft, not an authored scene instance.");
                        }
                        content.inspect(files, editor.selection, [&](forge::AssetId asset) {
                            prefab_editor.edit_source(files.document, asset);
                        });
                    } else if (selected.empty()) {
                        ImGui::TextWrapped(
                            "Select an object in the Scene or Hierarchy to edit its properties.");
                        forge::ui::help("Click a scene object or hierarchy row.");
                    }
                    for (const auto& e : doc["entities"])
                        if (e.at("id") == selected) {
                            ImGui::TextWrapped("Entity: %s | Authored",
                                               e.at("name").get_ref<const std::string&>().c_str());
                            forge::ui::help("This Inspector edits the authored entity; runtime "
                                            "values are shown in Game presentation.");
                            const auto name = e.at("name").get<std::string>();
                            if (name_entity != selected || authored_name != name) {
                                SDL_strlcpy(entity_name, name.c_str(), sizeof(entity_name));
                                name_entity = selected;
                                authored_name = name;
                            }
                            try {
                                if (editor.rename_entity) {
                                    ImGui::SetKeyboardFocusHere();
                                    editor.rename_entity = false;
                                }
                                if (ImGui::InputText("Name", entity_name, sizeof(entity_name),
                                                     ImGuiInputTextFlags_EnterReturnsTrue |
                                                         (e.contains("prefab_member")
                                                              ? ImGuiInputTextFlags_ReadOnly
                                                              : 0))) {
                                    forge::authoring_command(
                                        scene, "entity.rename",
                                        {{"entity", selected}, {"name", entity_name}});
                                }
                                forge::ui::help(e.contains("prefab_member")
                                                    ? "This name follows the prefab. Use Open "
                                                      "prefab source to rename this member."
                                                    : "Rename this entity. Press Enter to commit "
                                                      "one undoable edit.");
                                if (ImGui::TreeNode("Details")) {
                                    ImGui::TextWrapped("ID: %s", selected.c_str());
                                    forge::ui::help("Stable authored identity. Renaming does not "
                                                    "change this ID.");
                                    ImGui::TreePop();
                                }
                                forge::ui::help("Technical identity for this object.");
                                const auto parent = e.value("parent", std::string{});
                                std::string parent_name = "Scene root";
                                for (const auto& candidate : doc["entities"])
                                    if (candidate.at("id") == parent)
                                        parent_name = candidate.at("name").get<std::string>();
                                ImGui::BeginDisabled(e.contains("prefab_member"));
                                const bool choose_parent =
                                    ImGui::BeginCombo("Parent", parent_name.c_str());
                                ImGui::EndDisabled();
                                forge::ui::help(
                                    "Parent spatially with world placement preserved. The child "
                                    "then follows its parent. Unrepresentable local shear and "
                                    "cycles are rejected.");
                                std::string new_parent = parent;
                                if (choose_parent) {
                                    if (ImGui::Selectable("Scene root", parent.empty()))
                                        new_parent.clear();
                                    forge::ui::help("Place this entity at the scene root.");
                                    for (const auto& candidate : doc["entities"]) {
                                        const auto id = candidate.at("id").get<std::string>();
                                        if (id == selected)
                                            continue;
                                        ImGui::PushID(id.c_str());
                                        if (ImGui::Selectable(candidate.at("name")
                                                                  .get_ref<const std::string&>()
                                                                  .c_str(),
                                                              id == parent))
                                            new_parent = id;
                                        forge::ui::help(
                                            "Use this entity as the parent. A descendant "
                                            "cannot become its ancestor's parent.");
                                        ImGui::PopID();
                                    }
                                    ImGui::EndCombo();
                                }
                                if (new_parent != parent) {
                                    forge::authoring_command(
                                        scene, "entity.reparent",
                                        {{"entity", selected}, {"parent", new_parent}});
                                }
                            } catch (const std::exception& ex) {
                                message = ex.what();
                                editor.problems.report({"entity/" + selected,
                                                        "Error",
                                                        message,
                                                        selected,
                                                        {},
                                                        "Name / Parent",
                                                        scene.asset_id()});
                                forge::ui::field_error(message);
                            }
                            ImGui::TextDisabled("Reparent: preserve world, then follow parent");
                            forge::ui::help("Structural Parent changes also set Follow parent "
                                            "spatial binding. Cycles and unrepresentable shear are "
                                            "rejected without changing the scene.");
                            blockout.draw(scene, selected, message);
                            ImGui::BeginDisabled(blockout.active());
                            component_inspector.draw(scene, files.document, selected);
                            prefab_editor.inspector(scene, files.document, e);
                            ImGui::EndDisabled();
                        }
                    ImGui::EndDisabled();
                } else {
                    blockout.cancel();
                }
                ImGui::End();
            } else {
                blockout.cancel();
            }
            game_visible = false;
            for (const bool game_view : {false, true}) {
                auto& view_open = game_view ? workspace.game : workspace.scene;
                auto& view_camera = game_view ? game_camera : camera;
                auto& view_renderer = game_view ? game_viewport : viewport;
                auto& view_cache = game_view ? game_preview_snapshot : preview_snapshot;
                if (!view_open)
                    continue;
                if (game_view)
                    if (auto* settings = ImGui::FindWindowSettingsByID(ImHashStr("Scene")))
                        ImGui::SetNextWindowDockID(settings->DockId, ImGuiCond_FirstUseEver);
                if (game_view && focus_game) {
                    ImGui::SetNextWindowFocus();
                    focus_game = false;
                }

                if (ImGui::Begin(game_view ? "Game" : "Scene", &view_open,
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                     ImGuiWindowFlags_NoScrollbar)) {
                    if (game_view)
                        game_visible = true;
                    editor.task.focus(forge::ui::DocumentTask::Scene);
                    ImGui::Text("%s%s  |  %s",
                                files.document.path().empty()
                                    ? "Untitled"
                                    : forge::path_text(files.document.path().filename()).c_str(),
                                !game_view && files.document.dirty() ? " *" : "",
                                game_view ? (play.can_recover() ? "CRASHED / Recovery available"
                                             : !play.active()   ? "STOPPED"
                                             : !play.ready()    ? "STARTING / RECOVERING"
                                             : play.paused()    ? "PAUSED"
                                                                : "PLAYING")
                                          : "AUTHORING");
                    forge::ui::help("Active scene. * means unsaved changes. Play uses an isolated "
                                    "copy; stop play to edit.");
                    if (game_view && play.active()) {
                        game_input.controls(play);
                        runtime_ui.controls(play);
                    }
                    bool frame_selected = false, fit_scene = false;
                    if (!game_view) {
                        if (ImGui::Button("Create"))
                            ImGui::OpenPopup("Scene create");
                        forge::ui::help("Create a primitive for this authored scene. Also "
                                        "available in the Entity menu.");
                        if (ImGui::BeginPopup("Scene create")) {
                            if (ImGui::Checkbox("At view target", &blockout.at_view_target))
                                perform(save_preferences);
                            forge::ui::help("Place new primitives at the authoring camera pivot "
                                            "instead of the default origin.");
                            for (unsigned kind = 0; kind < 4; ++kind)
                                actions.item(std::string("Create / ") +
                                             forge::primitive_names[kind]);
                            ImGui::EndPopup();
                        }
                        if (ImGui::GetContentRegionAvail().x > 500 * forge::ui::interface_scale)
                            ImGui::SameLine();
                        ImGui::BeginDisabled(modal.active() || scene_tools.move.active());
                        try {
                            if (scene_tools.controls())
                                perform(save_preferences);
                            if (ImGui::GetContentRegionAvail().x > 700 * forge::ui::interface_scale)
                                ImGui::SameLine();
                            ImGui::BeginDisabled(selected.empty() || edit_locked);
                            if (forge::ui::button("Rotate (R)",
                                                  "Start world-axis rotation; X/Y/Z constrain, "
                                                  "Enter accepts, Escape cancels."))
                                modal.request(false);
                            ImGui::SameLine();
                            if (forge::ui::button("Scale (S)",
                                                  "Start local scaling; X/Y/Z constrain, Enter "
                                                  "accepts, Escape cancels."))
                                modal.request(true);
                            ImGui::EndDisabled();
                            if (ImGui::GetContentRegionAvail().x > 400 * forge::ui::interface_scale)
                                ImGui::SameLine();
                            if (ImGui::Button("View"))
                                ImGui::OpenPopup("##scene-view");
                            forge::ui::help(
                                "Frame objects, save/restore camera views, and configure "
                                "grid, snap, flight and orientation display.");
                            if (ImGui::BeginPopup("##scene-view")) {
                                ImGui::TextWrapped("Move / Rotate: World | Scale: Local");
                                forge::ui::help("Manipulator orientation, independent of the "
                                                "object's spatial parent binding.");
                                frame_selected = ImGui::MenuItem("Frame selected", "F");
                                forge::ui::help(
                                    "Center the camera on the selected visible object.");
                                fit_scene = ImGui::MenuItem("Fit scene");
                                forge::ui::help("Frame all visible objects.");
                                if (ImGui::MenuItem("Reset view"))
                                    view_camera = {};
                                forge::ui::help("Reset the editor camera without editing objects.");
                                if (ImGui::MenuItem("Save view"))
                                    perform([&] {
                                        forge::save_view(files.document, view_camera);
                                        message = "View bookmark saved";
                                    });
                                forge::ui::help("Remember one camera bookmark for this scene.");
                                if (ImGui::MenuItem("Restore view"))
                                    perform([&] {
                                        message = forge::restore_view(files.document, view_camera)
                                                      ? "View restored"
                                                      : "No saved view";
                                    });
                                forge::ui::help("Return to the saved camera bookmark.");
                                ImGui::Separator();
                                bool changed =
                                    ImGui::Checkbox("Orientation gizmo", &orientation.visible);
                                forge::ui::help("Show the clickable world-axis navigation widget.");
                                navigation_tools.overlay_control();
                                changed |= ImGui::Checkbox("Grid", &scene_tools.grid);
                                forge::ui::help(
                                    "Show the world-anchored XZ grid at Y=0, fading toward "
                                    "the horizon.");
                                changed |= ImGui::DragFloat("Snap spacing", &scene_tools.snap_step,
                                                            .05f, .01f, 1000, "%.2f",
                                                            ImGuiSliderFlags_AlwaysClamp);
                                forge::ui::help("Move snap spacing in world units.");
                                changed |= ImGui::DragFloat("Grid spacing", &scene_tools.grid_step,
                                                            .1f, .1f, 1000, "%.1f",
                                                            ImGuiSliderFlags_AlwaysClamp);
                                forge::ui::help(
                                    "Base grid spacing in world units. Coarser divisions "
                                    "fade in with distance; snap spacing is independent.");
                                changed |=
                                    ImGui::DragFloat("Fly speed", &scene_tools.fly_speed, .2f, .1f,
                                                     1000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                                forge::ui::help("RMB flight speed in world units per second.");
                                if (changed)
                                    perform(save_preferences);
                                ImGui::EndPopup();
                            }
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                        ImGui::EndDisabled();
                    }
                    view_camera.fly_speed = scene_tools.fly_speed;
                    if (game_view && !play.ready()) {
                        ImGui::TextWrapped("%s", play.status().c_str());
                        ImGui::TextWrapped("Use Play to start the isolated runtime. Scene remains "
                                           "your authored workspace.");
                        if (play.can_recover())
                            actions.button("recover");
                        ImGui::End();
                        continue;
                    }
                    auto read_preview = [&]() -> const forge::Json& {
                        return view_cache.get(
                            game_view ? play.snapshot_version() : scene.revision(), game_view,
                            blockout.active() || scene_tools.move.active() || modal.active(), [&] {
                                if (game_view)
                                    return play.ready() ? play.effective_snapshot()
                                                        : scene.effective_document();
                                auto source = authoring_snapshot.document(scene);
                                if (blockout.active())
                                    source = blockout.preview(source);
                                if (scene_tools.move.active())
                                    source = scene_tools.move.preview(source);
                                if (modal.active())
                                    source = modal.preview(source);
                                return (blockout.active() || scene_tools.move.active() ||
                                        modal.active())
                                           ? scene.preview_document(source)
                                           : authoring_snapshot.effective(scene);
                            });
                    };
                    const auto& preview = read_preview();
                    auto size = ImGui::GetContentRegionAvail();
                    if (size.x > 1 && size.y > 1) {
                        if (frame_selected &&
                            (selected.empty() ||
                             !view_camera.frame(preview, selected, size.x / size.y)))
                            message =
                                "Selected entity has no visible block, or exceeds camera range.";
                        if (fit_scene && !view_camera.frame(preview, "", size.x / size.y))
                            message = "No visible blocks to frame, or scene exceeds camera range.";
                        const auto image_origin = ImGui::GetCursorScreenPos();
                        const bool focused =
                            (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;
                        const bool popup = ImGui::IsPopupOpen(
                            nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
                        if (game_view)
                            game_input.viewport(image_origin, size);
                        const bool gizmo =
                            !game_view &&
                            orientation.input(view_camera, image_origin, size,
                                              focused && !game_input.captured() &&
                                                  !modal.active() && !scene_tools.move.active() &&
                                                  !popup);
                        ImGui::SetCursorScreenPos(image_origin);
                        forge::ui::ViewportInput input;
                        const bool was_modal = modal.active();
                        if (forge::ui::camera_controls(view_camera, size, focused, &input,
                                                       gizmo || was_modal || popup ||
                                                           game_input.captured())) {
                            if (selected.empty() ||
                                !view_camera.frame(preview, selected, size.x / size.y))
                                message = "Selected entity has no visible block, or exceeds camera "
                                          "range.";
                        }
                        const bool can_edit =
                            !game_view && focused && !play.active() && !native->busy() &&
                            !files.busy() && !blockout.active() &&
                            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                             ImGuiPopupFlags_AnyPopupLevel);
                        const auto mouse = ImGui::GetIO().MousePos;
                        const bool over_image =
                            ImGui::IsWindowHovered() && mouse.x >= image_origin.x &&
                            mouse.y >= image_origin.y && mouse.x < image_origin.x + size.x &&
                            mouse.y < image_origin.y + size.y;
                        if (!game_view)
                            modal.input(scene, selected, view_camera, image_origin, size,
                                        over_image && !gizmo,
                                        can_edit && !scene_tools.move.active(), message);
                        const bool previous_move_tool = scene_tools.move_tool;
                        if (!game_view)
                            scene_tools.input(
                                scene, view_camera, selected, image_origin, size, input,
                                can_edit && !gizmo && !was_modal && !modal.active(), message);
                        if (!game_view && input.activated &&
                            ImGui::IsMouseDown(ImGuiMouseButton_Left) && can_edit && !gizmo &&
                            !was_modal && !modal.active())
                            editor.selection.select_entity(selected);
                        if (previous_move_tool != scene_tools.move_tool)
                            perform(save_preferences);
                        const auto& rendered = read_preview();
                        const float render_scale =
                            std::min(1.0f, 4096.0f / std::max(size.x, size.y));
                        const auto scene_submit = forge::ui::Performance::Clock::now();
                        auto* texture = view_renderer.render(
                            context, rendered, unsigned(std::max(1.0f, size.x * render_scale)),
                            unsigned(std::max(1.0f, size.y * render_scale)), view_camera,
                            view_cache.generation(), game_view || performance.continuous,
                            {!game_view && scene_tools.grid, scene_tools.grid_step});
                        performance.scene_ms = forge::ui::Performance::milliseconds(
                            scene_submit, forge::ui::Performance::Clock::now());
                        if (game_view)
                            runtime_ui.draw(play, context, texture, image_origin, size,
                                            unsigned(std::max(1.0f, size.x * render_scale)),
                                            unsigned(std::max(1.0f, size.y * render_scale)));
                        ImGui::GetWindowDrawList()->AddImage(
                            ImTextureRef{reinterpret_cast<ImTextureID>(texture)}, image_origin,
                            {image_origin.x + size.x, image_origin.y + size.y});
                        if (!game_view)
                            scene_tools.draw(rendered, view_camera, selected, image_origin, size,
                                             can_edit && !modal.active());
                        forge::draw_animation_debug(rendered, view_camera, image_origin, size);
                        navigation_tools.draw(rendered, view_camera, image_origin, size);
                        if (!game_view)
                            orientation.draw(view_camera, image_origin, size);
                        if (!game_view)
                            modal.draw(image_origin, size);
                        ImGui::GetWindowDrawList()->AddText(
                            {image_origin.x + 8,
                             image_origin.y + 8 +
                                 (modal.active() ? ImGui::GetTextLineHeight() + 20 : 0)},
                            IM_COL32(185, 200, 215, 255),
                            (std::string(view_camera.view_name()) + " | Perspective").c_str());

                    } else if (!game_view) {
                        scene_tools.move.cancel();
                        modal.cancel();
                    }
                } else if (!game_view) {
                    scene_tools.move.cancel();
                    modal.cancel();
                }
                ImGui::End();
            }
            if (!workspace.scene) {
                scene_tools.move.cancel();
                modal.cancel();
            }
            if (!game_visible)
                game_input.release(play);
            if (workspace.content) {
                ImGui::BeginDisabled(modal.active() || scene_tools.move.active() ||
                                     blockout.active());
                content.draw(
                    files, &workspace.content,
                    [&] { prefab_editor.content(scene, files.document, selected, edit_locked); },
                    [&] {
                        animation_tools.content(files.document, edit_locked, message);
                        runtime_ui_tools.content(files.document, edit_locked, message);
                        navigation_tools.content(scene, files.document, edit_locked, message);
                    },
                    edit_locked);
                ImGui::EndDisabled();
            }
            prefab_editor.draw(scene, files.document, edit_locked);
            if (editor.reveal_content)
                workspace.content = true;
            if (workspace.problems) {
                if (auto* settings = ImGui::FindWindowSettingsByID(ImHashStr("Console")))
                    ImGui::SetNextWindowDockID(settings->DockId, ImGuiCond_FirstUseEver);
                if (editor.problems.draw(editor.selection, &workspace.problems)) {
                    workspace.inspector = true;
                    editor.task.owner = forge::ui::DocumentTask::Scene;
                }
            }
            if (auto* console = ImGui::FindWindowSettingsByID(ImHashStr("Console")))
                ImGui::SetNextWindowDockID(console->DockId, ImGuiCond_FirstUseEver);
            if (workspace.build) {
                if (ImGui::Begin("Gameplay Code###Native", &workspace.build)) {
                    forge::ui::heading("Gameplay", "C++ gameplay compiles outside the editor. Only "
                                                   "isolated runtimes load gameplay DLLs.");
                    ImGui::BeginDisabled(native->busy() || modal.active() ||
                                         scene_tools.move.active() || blockout.active());
                    try {
                        if (forge::ui::button(
                                "Create source",
                                "Create Native/gameplay.cpp and CMakeLists.txt. Existing "
                                "files are never overwritten."))
                            native->create_source();
                        if (forge::ui::button(
                                "Build & Reload",
                                "Incrementally compile, probe a unique candidate DLL, then reload "
                                "at a "
                                "runtime boundary. Failed builds retain the previous module."))
                            native->build();
                        if (ImGui::TreeNode("Compiler setup")) {
                            forge::ui::help(
                                "Advanced executable paths for local gameplay compilation.");
                            if (ImGui::InputText("CMake", cmake_path, sizeof(cmake_path))) {
                                native->cmake = cmake_path;
                                perform(save_preferences);
                            }
                            forge::ui::help(
                                "CMake executable path or command on PATH. Version 3.24 or "
                                "newer required.");
                            if (ImGui::InputText("Ninja", ninja_path, sizeof(ninja_path))) {
                                native->ninja = ninja_path;
                                perform(save_preferences);
                            }
                            forge::ui::help(
                                "Ninja executable path or command on PATH. Launch FORGE from an "
                                "x64 Native "
                                "Tools command prompt to provide the MSVC compiler environment.");
                            ImGui::TreePop();
                        }
                        forge::ui::help("Expand compiler and build-tool paths.");
                        if (ImGui::Checkbox("Build on save", &auto_build)) {
                            native->auto_build = auto_build;
                            perform(save_preferences);
                        }
                        forge::ui::help(
                            "Watch C/C++ headers, sources and CMake files in Native. Builds after "
                            "saves settle; play continues during compilation.");
                    } catch (const std::exception& e) {
                        message = e.what();
                    }
                    ImGui::EndDisabled();
                    ImGui::TextWrapped("%s", native->source_path().c_str());
                    forge::ui::help(
                        "Edit this source in your code editor. The sample moves entities "
                        "along the X axis.");
                    if (!native->log().empty()) {
                        forge::ui::heading("Build output",
                                           "Recent compiler output. The complete current log is in "
                                           "Project/.forge/native/build.log.");
                        ImGui::PushFont(log_font);
                        ImGui::TextUnformatted(native->log().c_str());
                        ImGui::PopFont();
                        forge::ui::help("Compiler diagnostics and build/validation results. Scroll "
                                        "horizontally for long source locations.");
                    }
                    ImGui::TextWrapped("%s", native->status().c_str());
                    forge::ui::help("Native build and candidate activation status.");
                    ImGui::TextWrapped(
                        "Windows: use Run-Forge-Dev.cmd or an x64 Native Tools command prompt. "
                        "Requires Visual Studio C++ tools, CMake and Ninja.");
                    forge::ui::help(
                        "These tools compile your gameplay locally. They are not needed to "
                        "open the editor.");
                }
                ImGui::End();
            }
            editor.record_status("Editor", message);
            editor.record_status("Files", files.status);
            editor.record_status("Runtime", play.status());
            editor.record_status("Build", native->status());
            if (!runtime_ui.diagnostic().empty())
                editor.problems.ingest({"runtime-ui/" + runtime_ui.diagnostic(),
                                        "Error",
                                        runtime_ui.diagnostic(),
                                        {},
                                        {},
                                        "Runtime UI",
                                        {}});
            if (!native->error().empty())
                editor.problems.ingest({"build/" + native->error(),
                                        "Error",
                                        native->error(),
                                        {},
                                        native->source_path(),
                                        {},
                                        {}});
            if (!files.error.empty())
                editor.problems.ingest({"files/" + files.error,
                                        "Error",
                                        files.error,
                                        {},
                                        forge::path_text(files.document.path()),
                                        {},
                                        scene.asset_id()});
            if (workspace.console) {
                if (ImGui::Begin("Console", &workspace.console,
                                 ImGuiWindowFlags_HorizontalScrollbar)) {
                    forge::ui::heading("Session log",
                                       "Chronological status changes for this session. Problems "
                                       "keeps actionable errors and warnings.");
                    if (forge::ui::button(
                            "Clear log",
                            "Clear the editor's displayed session log; project data is unchanged."))
                        editor.log.clear();
                    ImGui::PushFont(log_font);
                    for (const auto& line : editor.log)
                        ImGui::TextWrapped("%s", line.c_str());
                    ImGui::PopFont();
                    forge::ui::heading("Current details",
                                       "Current file/runtime state and low-level logs.");
                    ImGui::TextWrapped("%s", message.c_str());
                    forge::ui::help("Latest operation result.");
                    ImGui::TextWrapped("%s", files.status.c_str());
                    forge::ui::help("Latest project, file, autosave, or recovery result.");
                    ImGui::TextWrapped("Project: %s",
                                       forge::path_text(files.document.project()).c_str());
                    forge::ui::help("Current project root. Native builds, scenes, and recovery "
                                    "snapshots belong to this project.");
                    if (!play.log().empty()) {
                        ImGui::TextWrapped("%s", play.log().c_str());
                        forge::ui::help(
                            "Recent runtime error output, limited to 64 KiB per play session.");
                    }
                    ImGui::TextWrapped("%s", play.status().c_str());
                    if (play.active()) {
                        forge::draw_input_monitor(play);
                        const auto& timing = play.timing();
                        ImGui::Text(
                            "Tick: %llu | Fixed: %.0f Hz | Dropped: %llu | Clamped: %.3f s",
                            static_cast<unsigned long long>(timing.value("tick", std::uint64_t{})),
                            timing.value("simulation_hz", 60.0),
                            static_cast<unsigned long long>(
                                timing.value("dropped_ticks", std::uint64_t{})),
                            timing.value("clamped_seconds", 0.0));
                        forge::ui::help(
                            "Authoritative runtime tick count. Step adds exactly one. Overload "
                            "drops whole-tick debt; clamped seconds report long stalls. Fixed "
                            "timing alone does not guarantee deterministic gameplay.");
                    }
                    forge::ui::help("Play process status. A runtime failure leaves the editor and "
                                    "authored scene available.");
                }
                ImGui::End();
            }
            project_settings.draw(files.document, scene, edit_locked, message);
            if (prefab_editor.close_cancelled || project_settings.close_cancelled) {
                pending_switch.reset();
                prefab_editor.close_cancelled = project_settings.close_cancelled = false;
            }
            if (pending_switch) {
                if (!prefab_editor.dirty() && project_settings.dirty())
                    project_settings.request_close();
                else if (!prefab_editor.dirty() && !project_settings.dirty()) {
                    auto action = *pending_switch;
                    pending_switch.reset();
                    files.request(action);
                }
            }
            auto collect_diagnostics = [&](const auto& records) {
                for (const auto& d : records) {
                    const auto severity = d.value("severity", std::string{});
                    if (severity != "error" && severity != "warning" && severity != "fatal")
                        continue;
                    const forge::Json ctx = d.value("context", forge::Json::object());
                    forge::ui::Problem problem;
                    problem.key = d.dump();
                    problem.text = d.value("text", std::string{});
                    problem.severity = severity;
                    problem.entity = ctx.value("entity", std::string{});
                    problem.source = ctx.value("source", std::string{});
                    problem.property = ctx.value("property", std::string{});
                    if (ctx.contains("asset"))
                        problem.asset = ctx.at("asset").get<forge::AssetId>();
                    editor.problems.ingest(std::move(problem));
                }
            };
            collect_diagnostics(scene_engine.services().diagnostics());
            collect_diagnostics(play.diagnostics());
            if (panels_before != workspace.settings()) {
                try {
                    perform(save_preferences);
                } catch (const std::exception& e) {
                    message = e.what();
                }
            }
            performance.draw(viewport.redraws, viewport.retained);
            const auto ui_submit = forge::ui::Performance::Clock::now();
            auto* rtv = swap->GetCurrentBackBufferRTV();
            context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[] = {0.04f, 0.05f, 0.06f, 1};
            context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            ImGui::PopItemFlag();
            gui->Render(context);
#ifdef FORGE_UI_FIXTURE
            ++fixture.frames;
            if (fixture.prepared && fixture.frames > 12 &&
                (fixture.stage != 6 || (play.control_ready() && !play.paused())) &&
                (fixture.stage != 7 || (play.paused() && game_input.captured()))) {
                fixture.capture(device, context, rtv);
                if (fixture.stage == 12) {
                    play.stop();
                    running = false;
                }
            }
#endif
            const auto present = forge::ui::Performance::Clock::now();
            swap->Present(0);
            performance.finish(ui_submit, present);
        }
        if (startup_layout.save_enabled)
            ImGui::SaveIniSettingsToDisk(ini.c_str());
        context->Flush();
        context->WaitForIdle();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
#ifndef FORGE_UI_FIXTURE
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FORGE could not continue", e.what(),
                                 window.get());
#endif
        result = 1;
    }
    window.reset();
    SDL_Quit();
    return result;
}
