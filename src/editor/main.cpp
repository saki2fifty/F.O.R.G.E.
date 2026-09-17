#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "ImGuiImplSDL3.hpp"
#include "automation.hpp"
#include "blockout.hpp"
#include "camera_controls.hpp"
#include "command_workspace.hpp"
#include "content.hpp"
#include "files.hpp"
#include "game_input.hpp"
#include "help.hpp"
#include "hierarchy.hpp"
#include "native_build.hpp"
#include "orientation.hpp"
#include "performance.hpp"
#include "physics_inspector.hpp"
#include "play.hpp"
#include "prefabs.hpp"
#include "project_settings.hpp"
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
        auto* factory = LoadAndGetEngineFactoryD3D12();
        if (!factory)
            throw std::runtime_error("D3D12 backend unavailable");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        RefCntAutoPtr<ISwapChain> swap;
        EngineD3D12CreateInfo engine;
        factory->CreateDeviceAndContextsD3D12(engine, &device, &context);
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
        // Keep the existing face instead of the new scale-dependent default selection.
        ImGui::GetIO().Fonts->AddFontDefaultBitmap();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        char* preferences = SDL_GetPrefPath("FORGE", "Editor");
        if (!preferences)
            throw std::runtime_error("Cannot locate editor preferences directory");
        const std::filesystem::path config = preferences;
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
        forge::ui::AutomationWorkspace automation;
        forge::PlaySession play;
        forge::GameInput game_input;
        forge::ProjectSettingsEditor project_settings;
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("Cannot locate runtime directory");
        const auto runtime_path = (std::filesystem::path(base) / "forge_runtime.exe").string();
        forge::EditorFiles files(scene, window.get(), recent_projects);
        std::string message =
            "Ready. Block preview is an authoring diagnostic, not the final game renderer.";
        auto perform = [&](auto&& action) {
            try {
                action();
            } catch (const std::exception& e) {
                message = e.what();
                forge::Diagnostic diagnostic{forge::Severity::Error, "editor", message, {}};
                diagnostic.context.asset = scene.asset_id();
                scene_engine.services().emit(std::move(diagnostic));
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
            if (argc > 1)
                files.start(std::filesystem::u8path(argv[1]));
            else if (!last_project.empty())
                files.start(std::filesystem::u8path(last_project));
            else {
                open_scratch();
                message = "Scratch project opened. Use File > New project or Open project for your "
                          "own game.";
            }
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
        forge::Viewport viewport(device);
        forge::AuthoringSnapshot authoring_snapshot;
        forge::PrefabEditor prefab_editor;
        forge::PreviewSnapshot preview_snapshot;
        forge::EditorCamera camera;
        try {
            forge::restore_view(files.document, camera);
        } catch (const std::exception& e) {
            message = e.what();
        }
        forge::Telemetry telemetry;
        forge::ui::Performance performance;
        std::string selected, name_entity, authored_name;
        char entity_name[1024]{};
        bool running = true;
        std::string current_title;
        while (running) {
            performance.begin();
            SDL_Event event;
            game_input.pump(play,
                            workspace.scene && !files.busy() && !ImGui::GetIO().WantTextInput &&
                                !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                                 ImGuiPopupFlags_AnyPopupLevel) &&
                                (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS));
            while (SDL_PollEvent(&event)) {
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
                selected.clear();
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
            forge::ui::status_bar(telemetry, play.active(), scene.entity_count());
            const auto dock = ImGui::DockSpaceOverViewport();
            if (initialize_layout || workspace.reset) {
                workspace.reset = false;
                forge::ui::initialize_workspace(dock);
                initialize_layout = false;
            }
            const bool edit_locked = play.active() || native->busy() || files.busy() ||
                                     scene_tools.move.active() || modal.active() ||
                                     blockout.active();
            const auto panels_before = workspace.settings();
            if (forge::ui::begin_toolbar()) {
                ImGui::BeginDisabled(edit_locked);
                files.menu();
                ImGui::EndDisabled();
                commands.menu([&] {
                    automation.menu();
                    performance.menu();
                    project_settings.menu();
                });
                if (workspace.menu()) {
                    try {
                        perform(save_preferences);
                    } catch (const std::exception& e) {
                        message = e.what();
                    }
                }
                forge::ui::help_menu(std::filesystem::path(base), message);
                if (automation.active()) {
                    ImGui::TextColored({.5f, .8f, 1, 1}, "API on");
                    forge::ui::help("A local automation connection is active. Tools > Automation > "
                                    "Stop connection revokes access.");
                }
                try {
                    ImGui::Separator();
                    ImGui::BeginDisabled(edit_locked);
                    if (forge::ui::button(
                            "Save",
                            "Save the active scene. Ctrl+S. Untitled scenes ask for a location."))
                        files.save();
                    if (forge::ui::button("Undo", "Restore the previous authored scene edit."))
                        perform([&] { forge::authoring_history(scene, false); });
                    if (forge::ui::button("Redo", "Reapply the last undone edit."))
                        perform([&] { forge::authoring_history(scene, true); });
                    if (ImGui::BeginMenu("Create")) {
                        if (ImGui::Checkbox("At view target", &blockout.at_view_target))
                            perform(save_preferences);
                        forge::ui::help("Create the primitive at the camera's orbit target instead "
                                        "of the default origin placement.");
                        for (unsigned kind = 0; kind < 4; ++kind) {
                            if (ImGui::MenuItem(forge::primitive_names[kind]))
                                perform([&] {
                                    selected = forge::create_primitive(
                                        scene, kind,
                                        blockout.at_view_target
                                            ? camera.target
                                            : forge::Float3{0, kind == 3 ? 0.0f : 1.0f, 0});
                                });
                            forge::ui::help("Create a built-in primitive with reflected position, "
                                            "rotation, scale, and opaque color. Undo removes it.");
                        }
                        ImGui::EndMenu();
                    }
                    forge::ui::help(
                        "Create Cube, Sphere, Cylinder, or Plane primitives for scene blockout.");
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    ImGui::BeginDisabled(native->busy() || files.busy() || modal.active() ||
                                         scene_tools.move.active() || blockout.active());
                    if (forge::ui::button(
                            play.active() ? "Restart" : "Play",
                            "Start a fresh isolated play world from the current authored scene."))
                        perform([&] {
                            if (files.document.settings().requires_native_sdk())
                                throw std::runtime_error(
                                    "This project requires the experimental native SDK runtime. "
                                    "Use forge_runtime --sdk-project; editor Play currently "
                                    "supports ABI1.");
                            play.stop();
                            play.configure(files.document.settings().simulation_hz(),
                                           files.document.settings().input(),
                                           files.document.settings().physics().gravity);
                            play.start(runtime_path, scene.snapshot(), native->artifact());
                        });
                    if (play.can_recover() &&
                        forge::ui::button(
                            "Recover",
                            "Resume the last completed play checkpoint with its previous module."))
                        perform([&] { play.recover(); });
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!play.control_ready());
                    if (forge::ui::button(play.paused() ? "Resume" : "Pause",
                                          "Pause fixed simulation ticks, or resume the runtime "
                                          "clock. Reload does not accumulate time debt.")) {
                        if (play.paused())
                            play.resume();
                        else
                            play.pause();
                    }
                    ImGui::BeginDisabled(!play.paused());
                    if (forge::ui::button("Step",
                                          "While paused, run exactly one fixed simulation tick and "
                                          "remain paused. Also validates a pending reload."))
                        play.step();
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!play.active());
                    if (forge::ui::button("Stop",
                                          "Stop gameplay and return to your authored scene."))
                        play.stop();
                    ImGui::EndDisabled();
                    if (ImGui::Checkbox("Tooltips", &forge::ui::tooltips))
                        perform(save_preferences);
                    forge::ui::help("Show contextual help for editor controls, fields, and section "
                                    "headers. Saved between sessions.");
                } catch (const std::exception& e) {
                    message = e.what();
                }
                ImGui::Text("UI %.0f%%", forge::ui::interface_scale * 100);
                forge::ui::help("Interface zoom: Ctrl+Minus / Ctrl+Plus (or Ctrl+Equals). "
                                "Ctrl+0 resets to 100%. Saved between sessions.");
                forge::ui::end_toolbar();
            }
            if (!edit_locked)
                files.shortcuts();
            if (!edit_locked &&
                !ImGui::IsPopupOpen(nullptr,
                                    ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
                !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                ImGui::GetIO().KeyCtrl) {
                try {
                    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                        if (ImGui::GetIO().KeyShift)
                            perform([&] { forge::authoring_history(scene, true); });
                        else
                            perform([&] { forge::authoring_history(scene, false); });
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                        perform([&] { forge::authoring_history(scene, true); });
                    if (!selected.empty() && ImGui::IsKeyPressed(ImGuiKey_D, false))
                        selected = forge::authoring_command(scene, "entity.duplicate",
                                                            {{"entity", selected}})
                                       .at("selected")
                                       .get<std::string>();
                } catch (const std::exception& e) {
                    message = e.what();
                }
            }
            commands.draw(scene, selected, message, scene_tools.snap_step, camera.target,
                          blockout.at_view_target, edit_locked);
            files.draw_dialogs();
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
                    forge::ui::hierarchy(doc, selected, hierarchy_filter, expand);
                    if (ImGui::IsWindowFocused() && !edit_locked && !ImGui::GetIO().WantTextInput &&
                        !ImGui::IsAnyItemActive() && !selected.empty() &&
                        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                        try {
                            forge::authoring_command(scene, "entity.delete",
                                                     {{"entity", selected}});
                            selected.clear();
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                    }
                }
                ImGui::End();
            }
            if (workspace.inspector) {
                if (ImGui::Begin("Inspector", &workspace.inspector)) {
                    ImGui::BeginDisabled(play.active() || native->busy() || files.busy() ||
                                         scene_tools.move.active() || modal.active());
                    if (selected.empty()) {
                        ImGui::TextWrapped(
                            "Select an object in the Scene or Hierarchy to edit its properties.");
                        forge::ui::help("Click a scene object or hierarchy row.");
                    }
                    for (const auto& e : doc["entities"])
                        if (e.at("id") == selected) {
                            const auto name = e.at("name").get<std::string>();
                            if (name_entity != selected || authored_name != name) {
                                SDL_strlcpy(entity_name, name.c_str(), sizeof(entity_name));
                                name_entity = selected;
                                authored_name = name;
                            }
                            try {
                                prefab_editor.inspector(scene, files.document, e);
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
                            }
                            blockout.draw(scene, selected, message);
                            ImGui::BeginDisabled(blockout.active());
                            forge::physics_inspector(scene, selected, message);
                            if (ImGui::Button("Object actions"))
                                ImGui::OpenPopup("##object-actions");
                            forge::ui::help("Duplicate or delete this object and its children; "
                                            "ground or snap its position.");
                            if (ImGui::BeginPopup("##object-actions")) {
                                try {
                                    if (ImGui::MenuItem("Duplicate subtree", "Ctrl+D", false,
                                                        !e.contains("prefab_member")))
                                        selected =
                                            forge::authoring_command(scene, "entity.duplicate",
                                                                     {{"entity", selected}})
                                                .at("selected")
                                                .get<std::string>();
                                    forge::ui::help("Duplicate this object and descendants with "
                                                    "new identities.");
                                    if (ImGui::MenuItem("Delete subtree", "Delete", false,
                                                        !e.contains("prefab_member"))) {
                                        forge::authoring_command(scene, "entity.delete",
                                                                 {{"entity", selected}});
                                        selected.clear();
                                    }
                                    forge::ui::help(
                                        "Delete this object and descendants. Undo restores them.");
                                    if (ImGui::MenuItem("Place on ground"))
                                        forge::authoring_command(scene, "transform.ground",
                                                                 {{"entity", selected}});
                                    forge::ui::help(
                                        "Place the transformed mesh bottom at world Y=0.");
                                    if (ImGui::MenuItem("Snap position"))
                                        forge::authoring_command(scene, "transform.snap",
                                                                 {{"entity", selected},
                                                                  {"step", scene_tools.snap_step}});
                                    forge::ui::help(
                                        "Round position to the Scene View snap spacing.");
                                } catch (const std::exception& ex) {
                                    message = ex.what();
                                }
                                ImGui::EndPopup();
                            }
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
            if (workspace.scene) {
                if (ImGui::Begin("Scene", &workspace.scene,
                                 ImGuiWindowFlags_NoScrollWithMouse |
                                     ImGuiWindowFlags_NoScrollbar)) {
                    ImGui::Text("%s%s  |  %s",
                                files.document.path().empty()
                                    ? "Untitled"
                                    : forge::path_text(files.document.path().filename()).c_str(),
                                files.document.dirty() ? " *" : "",
                                play.active() ? "PLAY" : "EDIT");
                    forge::ui::help("Active scene. * means unsaved changes. Play uses an isolated "
                                    "copy; stop play to edit.");
                    if (play.active())
                        game_input.controls(play);
                    bool frame_selected = false, fit_scene = false;
                    ImGui::BeginDisabled(modal.active() || scene_tools.move.active());
                    try {
                        if (scene_tools.controls())
                            perform(save_preferences);
                        ImGui::SameLine();
                        if (ImGui::Button("View"))
                            ImGui::OpenPopup("##scene-view");
                        forge::ui::help("Frame objects, save/restore camera views, and configure "
                                        "grid, snap, flight and orientation display.");
                        if (ImGui::BeginPopup("##scene-view")) {
                            frame_selected = ImGui::MenuItem("Frame selected", "F");
                            forge::ui::help("Center the camera on the selected visible object.");
                            fit_scene = ImGui::MenuItem("Fit scene");
                            forge::ui::help("Frame all visible objects.");
                            if (ImGui::MenuItem("Reset view"))
                                camera = {};
                            forge::ui::help("Reset the editor camera without editing objects.");
                            if (ImGui::MenuItem("Save view"))
                                perform([&] {
                                    forge::save_view(files.document, camera);
                                    message = "View bookmark saved";
                                });
                            forge::ui::help("Remember one camera bookmark for this scene.");
                            if (ImGui::MenuItem("Restore view"))
                                perform([&] {
                                    message = forge::restore_view(files.document, camera)
                                                  ? "View restored"
                                                  : "No saved view";
                                });
                            forge::ui::help("Return to the saved camera bookmark.");
                            ImGui::Separator();
                            bool changed =
                                ImGui::Checkbox("Orientation gizmo", &orientation.visible);
                            forge::ui::help("Show the clickable world-axis navigation widget.");
                            changed |= ImGui::Checkbox("Grid", &scene_tools.grid);
                            forge::ui::help("Show the world-anchored XZ grid at Y=0, fading toward "
                                            "the horizon.");
                            changed |=
                                ImGui::DragFloat("Snap spacing", &scene_tools.snap_step, .05f, .01f,
                                                 1000, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                            forge::ui::help("Move snap spacing in world units.");
                            changed |=
                                ImGui::DragFloat("Grid spacing", &scene_tools.grid_step, .1f, .1f,
                                                 1000, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                            forge::ui::help("Base grid spacing in world units. Coarser divisions "
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
                    camera.fly_speed = scene_tools.fly_speed;
                    auto read_preview = [&]() -> const forge::Json& {
                        return preview_snapshot.get(
                            play.active() ? play.snapshot_version() : scene.revision(),
                            play.active(),
                            blockout.active() || scene_tools.move.active() || modal.active(), [&] {
                                if (play.active())
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
                            (selected.empty() || !camera.frame(preview, selected, size.x / size.y)))
                            message =
                                "Selected entity has no visible block, or exceeds camera range.";
                        if (fit_scene && !camera.frame(preview, "", size.x / size.y))
                            message = "No visible blocks to frame, or scene exceeds camera range.";
                        const auto image_origin = ImGui::GetCursorScreenPos();
                        const bool focused =
                            (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_INPUT_FOCUS) != 0;
                        const bool popup = ImGui::IsPopupOpen(
                            nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
                        const bool gizmo = orientation.input(
                            camera, image_origin, size,
                            focused && !game_input.captured() && !modal.active() &&
                                !scene_tools.move.active() && !popup);
                        ImGui::SetCursorScreenPos(image_origin);
                        forge::ui::ViewportInput input;
                        const bool was_modal = modal.active();
                        if (forge::ui::camera_controls(camera, size, focused, &input,
                                                       gizmo || was_modal || popup ||
                                                           game_input.captured())) {
                            if (selected.empty() ||
                                !camera.frame(preview, selected, size.x / size.y))
                                message = "Selected entity has no visible block, or exceeds camera "
                                          "range.";
                        }
                        const bool can_edit =
                            focused && !play.active() && !native->busy() && !files.busy() &&
                            !blockout.active() &&
                            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                                             ImGuiPopupFlags_AnyPopupLevel);
                        const auto mouse = ImGui::GetIO().MousePos;
                        const bool over_image =
                            ImGui::IsWindowHovered() && mouse.x >= image_origin.x &&
                            mouse.y >= image_origin.y && mouse.x < image_origin.x + size.x &&
                            mouse.y < image_origin.y + size.y;
                        modal.input(scene, selected, camera, image_origin, size,
                                    over_image && !gizmo, can_edit && !scene_tools.move.active(),
                                    message);
                        const bool previous_move_tool = scene_tools.move_tool;
                        scene_tools.input(scene, camera, selected, image_origin, size, input,
                                          can_edit && !gizmo && !was_modal && !modal.active(),
                                          message);
                        if (previous_move_tool != scene_tools.move_tool)
                            perform(save_preferences);
                        const auto& rendered = read_preview();
                        const float render_scale =
                            std::min(1.0f, 4096.0f / std::max(size.x, size.y));
                        const auto scene_submit = forge::ui::Performance::Clock::now();
                        auto* texture = viewport.render(
                            context, rendered, unsigned(std::max(1.0f, size.x * render_scale)),
                            unsigned(std::max(1.0f, size.y * render_scale)), camera,
                            preview_snapshot.generation(), play.active() || performance.continuous,
                            {scene_tools.grid, scene_tools.grid_step});
                        performance.scene_ms = forge::ui::Performance::milliseconds(
                            scene_submit, forge::ui::Performance::Clock::now());
                        ImGui::GetWindowDrawList()->AddImage(
                            ImTextureRef{reinterpret_cast<ImTextureID>(texture)}, image_origin,
                            {image_origin.x + size.x, image_origin.y + size.y});
                        scene_tools.draw(rendered, camera, selected, image_origin, size,
                                         can_edit && !modal.active());
                        orientation.draw(camera, image_origin, size);
                        modal.draw(image_origin, size);
                        ImGui::GetWindowDrawList()->AddText(
                            {image_origin.x + 8,
                             image_origin.y + 8 +
                                 (modal.active() ? ImGui::GetTextLineHeight() + 20 : 0)},
                            IM_COL32(185, 200, 215, 255),
                            (std::string(camera.view_name()) + " | Perspective").c_str());

                    } else {
                        scene_tools.move.cancel();
                        modal.cancel();
                    }
                } else {
                    scene_tools.move.cancel();
                    modal.cancel();
                }
                ImGui::End();
            } else {
                scene_tools.move.cancel();
                modal.cancel();
            }
            if (workspace.content) {
                ImGui::BeginDisabled(modal.active() || scene_tools.move.active() ||
                                     blockout.active() || play.active());
                content.draw(files, &workspace.content, [&] {
                    prefab_editor.content(scene, files.document, selected, edit_locked);
                });
                prefab_editor.draw(scene, files.document, edit_locked);
                ImGui::EndDisabled();
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
                        ImGui::TextUnformatted(native->log().c_str());
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
            if (workspace.console) {
                if (ImGui::Begin("Console", &workspace.console,
                                 ImGuiWindowFlags_HorizontalScrollbar)) {
                    forge::ui::heading("Status",
                                       "Latest editor operation or validation diagnostic.");
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
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "FORGE could not continue", e.what(),
                                 window.get());
        result = 1;
    }
    window.reset();
    SDL_Quit();
    return result;
}
