#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "ImGuiImplSDL3.hpp"
#include "hierarchy.hpp"
#include "native_build.hpp"
#include "play.hpp"
#include "viewport.hpp"
#include "widgets.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
#include <memory>
using namespace Diligent;
int main(int argc, char** argv) {
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
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        char* preferences = SDL_GetPrefPath("FORGE", "Editor");
        if (!preferences)
            throw std::runtime_error("Cannot locate editor preferences directory");
        const std::filesystem::path config = preferences;
        SDL_free(preferences);
        ini = (config / "workspace.ini").string();
        ImGui::GetIO().IniFilename = ini.c_str();
        const auto settings = config / "settings.json";
        char cmake_path[1024] = "cmake";
        char ninja_path[1024] = "ninja";
        bool auto_build = false;
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
                auto_build = j.value("auto_build", false);
            }
        } catch (const std::exception&) { /* Recover malformed user preferences with defaults. */
        }
        auto save_preferences = [&] {
            forge::atomic_write(settings,
                                forge::Json{{"tooltips", forge::ui::tooltips},
                                            {"interface_scale", forge::ui::interface_scale},
                                            {"cmake", cmake_path},
                                            {"ninja", ninja_path},
                                            {"auto_build", auto_build}}
                                    .dump(2));
        };
        forge::Scene scene;
        forge::PlaySession play;
        const char* base = SDL_GetBasePath();
        if (!base)
            throw std::runtime_error("Cannot locate runtime directory");
        const auto runtime_path = (std::filesystem::path(base) / "forge_runtime.exe").string();
        const std::filesystem::path project =
            argc > 1 ? std::filesystem::absolute(argv[1]) : std::filesystem::current_path();
        const auto scene_path = project / "main.scene.json";
        forge::NativeBuild native(project, std::filesystem::path(base) / "sdk", runtime_path);
        native.cmake = cmake_path;
        native.ninja = ninja_path;
        native.auto_build = auto_build;
        std::string message =
            "Ready. Block preview is an authoring diagnostic, not the final game renderer.";
        if (std::filesystem::exists(scene_path))
            scene.load(scene_path);
        bool initialize_layout = !std::filesystem::exists(ini);
        forge::Viewport viewport(device);
        forge::EditorCamera camera;
        int camera_drag = -1;
        std::string selected, name_entity, authored_name;
        char entity_name[1024]{};
        bool running = true;
        unsigned next_id = 1;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
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
                            save_preferences();
                        } catch (const std::exception& e) {
                            message = e.what();
                        }
                    }
                }
                if (event.type == SDL_EVENT_QUIT)
                    running = false;
            }
            play.pump();
            native.pump(play, scene.document());
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window.get(), &width, &height);
            if (width <= 0 || height <= 0 ||
                SDL_GetWindowFlags(window.get()) & SDL_WINDOW_MINIMIZED) {
                SDL_Delay(20);
                continue;
            }
            if (swap->GetDesc().Width != unsigned(width) ||
                swap->GetDesc().Height != unsigned(height))
                swap->Resize(width, height);
            gui->NewFrame(width, height, swap->GetDesc().PreTransform);
            const auto dock = ImGui::DockSpaceOverViewport();
            if (initialize_layout) {
                forge::ui::initialize_workspace(dock);
                initialize_layout = false;
            }
            if (ImGui::BeginMainMenuBar()) {
                try {
                    if (forge::ui::button(
                            "Save", "Atomically save the authored scene to main.scene.json.")) {
                        scene.save(scene_path);
                        message = "Scene saved.";
                    }
                    if (forge::ui::button("Undo", "Restore the previous authored scene edit."))
                        scene.undo();
                    if (forge::ui::button("Redo", "Reapply the last undone edit."))
                        scene.redo();
                    if (forge::ui::button(
                            "Add entity",
                            "Create an entity with a stable ID and an editable position.")) {
                        auto doc = scene.document();
                        std::string id;
                        bool found;
                        do {
                            id = "entity-" + std::to_string(next_id++);
                            found = false;
                            for (const auto& e : doc["entities"])
                                found |= e["id"] == id;
                        } while (found);
                        doc["entities"].push_back(
                            {{"id", id},
                             {"name", id},
                             {"components", {{"forge.position", {{"x", 0}, {"y", 1}, {"z", 0}}}}}});
                        scene.edit(doc);
                        selected = id;
                    }
                    ImGui::BeginDisabled(native.busy());
                    if (forge::ui::button(
                            play.active() ? "Restart" : "Play",
                            "Start a fresh isolated play world from the current authored scene."))
                        play.start(runtime_path, scene.document(), native.artifact());
                    if (play.can_recover() &&
                        forge::ui::button(
                            "Recover",
                            "Resume the last completed play checkpoint with its previous module."))
                        play.recover();
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!play.active());
                    if (forge::ui::button("Stop",
                                          "Stop gameplay and return to your authored scene."))
                        play.stop();
                    ImGui::EndDisabled();
                    if (ImGui::Checkbox("Tooltips", &forge::ui::tooltips))
                        save_preferences();
                    forge::ui::help("Show contextual help for editor controls, fields, and section "
                                    "headers. Saved between sessions.");
                } catch (const std::exception& e) {
                    message = e.what();
                }
                ImGui::Text("UI %.0f%%", forge::ui::interface_scale * 100);
                forge::ui::help("Interface zoom: Ctrl+Minus / Ctrl+Plus (or Ctrl+Equals). "
                                "Ctrl+0 resets to 100%. Saved between sessions.");
                ImGui::EndMainMenuBar();
            }
            auto doc = scene.document();
            const auto schema = scene.schema();
            if (ImGui::Begin("World")) {
                forge::ui::heading("Entities",
                                   "Authored entities identified by stable project IDs.");
                forge::ui::hierarchy(doc, selected);
            }
            ImGui::End();
            if (ImGui::Begin("Inspector")) {
                forge::ui::heading("Properties", "Position properties use world units. Changes are "
                                                 "undoable and serialize with the scene.");
                for (auto& e : doc["entities"])
                    if (e.at("id") == selected) {
                        const auto name = e.at("name").get<std::string>();
                        if (name_entity != selected || authored_name != name) {
                            SDL_strlcpy(entity_name, name.c_str(), sizeof(entity_name));
                            name_entity = selected;
                            authored_name = name;
                        }
                        try {
                            if (ImGui::InputText("Name", entity_name, sizeof(entity_name),
                                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                                scene.rename_entity(selected, entity_name);
                                e["name"] = entity_name;
                            }
                            forge::ui::help(
                                "Rename this entity. Press Enter to commit one undoable edit.");
                            ImGui::TextWrapped("ID: %s", selected.c_str());
                            forge::ui::help(
                                "Stable authored identity. Renaming does not change this ID.");
                            const auto parent = e.value("parent", std::string{});
                            const bool choose_parent = ImGui::BeginCombo(
                                "Parent", parent.empty() ? "Scene root" : parent.c_str());
                            forge::ui::help("Change the Flecs ChildOf relationship. Positions "
                                            "remain in world units. Cycles are rejected.");
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
                                    forge::ui::help("Use this entity as the parent. A descendant "
                                                    "cannot become its ancestor's parent.");
                                    ImGui::PopID();
                                }
                                ImGui::EndCombo();
                            }
                            if (new_parent != parent) {
                                scene.reparent_entity(selected, new_parent);
                                if (new_parent.empty())
                                    e.erase("parent");
                                else
                                    e["parent"] = new_parent;
                            }
                            if (forge::ui::button(
                                    "Duplicate subtree",
                                    "Copy this entity and its descendants with new stable IDs. "
                                    "Undo restores the previous scene.")) {
                                selected = scene.duplicate_subtree(selected);
                                break;
                            }
                            if (forge::ui::button(
                                    "Delete subtree",
                                    "Delete this entity and all descendants. Undo restores them. "
                                    "Prefabs used outside the subtree cannot be deleted.")) {
                                scene.delete_subtree(selected);
                                selected.clear();
                                break;
                            }
                        } catch (const std::exception& ex) {
                            message = ex.what();
                        }
                        if (e["components"].contains("forge.position")) {
                            auto& p = e["components"]["forge.position"];
                            for (const auto& field : schema.at("components").at(0).at("fields")) {
                                const auto field_name = field.at("id").get<std::string>();
                                const auto help = field.at("description").get<std::string>();
                                const char* axis = field_name.c_str();
                                float value = p.at(axis).get<float>();
                                if (forge::ui::scalar(axis, &value, help.c_str())) {
                                    p[axis] = value;
                                    try {
                                        scene.edit(doc);
                                    } catch (const std::exception& ex) {
                                        message = ex.what();
                                    }
                                }
                            }
                        }
                    }
            }
            ImGui::End();
            if (ImGui::Begin("Scene", nullptr,
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
                forge::ui::heading(
                    "Block preview",
                    "Diligent renders a cube for each entity with a position. "
                    "Orbit, pan, and frame this editor-only camera without editing the scene.");
                ImGui::TextUnformatted(play.active() ? "PLAY | isolated scene copy"
                                                     : "EDIT | authored scene");
                forge::ui::help(
                    "During play this preview shows runtime positions. Inspector edits "
                    "still affect authoring; Restart applies them to a fresh play world.");
                const auto& preview = play.active() ? play.snapshot() : doc;
                const float toolbar_width =
                    ImGui::CalcTextSize("Frame selected").x + ImGui::CalcTextSize("Fit scene").x +
                    ImGui::CalcTextSize("Reset view").x + 6 * ImGui::GetStyle().FramePadding.x +
                    2 * ImGui::GetStyle().ItemSpacing.x;
                const bool horizontal = ImGui::GetContentRegionAvail().x >= toolbar_width;
                const bool frame_selected =
                    forge::ui::button("Frame selected", "Center the camera on the selected visible "
                                                        "block. Shortcut: F over the viewport.");
                if (horizontal)
                    ImGui::SameLine();
                const bool fit_scene = forge::ui::button(
                    "Fit scene",
                    "Fit all visible blocks, accounting for the viewport aspect ratio.");
                if (horizontal)
                    ImGui::SameLine();
                if (forge::ui::button("Reset view", "Reset the editor camera to its startup "
                                                    "position. Scene data is unchanged."))
                    camera = forge::EditorCamera{};
                auto size = ImGui::GetContentRegionAvail();
                if (size.x > 1 && size.y > 1) {
                    if (frame_selected &&
                        (selected.empty() || !camera.frame(preview, selected, size.x / size.y)))
                        message = "Selected entity has no visible block, or exceeds camera range.";
                    if (fit_scene && !camera.frame(preview, "", size.x / size.y))
                        message = "No visible blocks to frame, or scene exceeds camera range.";
                    auto* texture =
                        viewport.render(context, play.active() ? play.snapshot() : doc,
                                        unsigned(std::clamp(size.x, 1.0f, 4096.0f)),
                                        unsigned(std::clamp(size.y, 1.0f, 4096.0f)), camera);
                    ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(texture)}, size);
                    const bool hovered = ImGui::IsItemHovered();
                    const auto& io = ImGui::GetIO();
                    if (!ImGui::IsWindowFocused() ||
                        (camera_drag >= 0 && !ImGui::IsMouseDown(camera_drag)))
                        camera_drag = -1;
                    if (hovered) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                            camera_drag = ImGuiMouseButton_Right;
                        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                            camera_drag = ImGuiMouseButton_Middle;
                        if (!io.KeyCtrl)
                            camera.zoom(io.MouseWheel);
                        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                            if (selected.empty() ||
                                !camera.frame(preview, selected, size.x / size.y))
                                message = "Selected entity has no visible block, or exceeds camera "
                                          "range.";
                        }
                    }
                    if (camera_drag == ImGuiMouseButton_Right)
                        camera.orbit(io.MouseDelta.x, io.MouseDelta.y);
                    else if (camera_drag == ImGuiMouseButton_Middle)
                        camera.pan(io.MouseDelta.x, io.MouseDelta.y, size.y);
                    forge::ui::help(
                        "Right-drag: orbit. Middle-drag: pan. Wheel: zoom. F: frame selected. "
                        "Camera navigation changes neither the authored scene nor gameplay.");
                } else {
                    camera_drag = -1;
                }
            } else {
                camera_drag = -1;
            }
            ImGui::End();
            if (auto* console = ImGui::FindWindowSettingsByID(ImHashStr("Console")))
                ImGui::SetNextWindowDockID(console->DockId, ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Native")) {
                forge::ui::heading("Gameplay", "C++ gameplay compiles outside the editor. Only "
                                               "isolated runtimes load gameplay DLLs.");
                ImGui::BeginDisabled(native.busy());
                try {
                    if (forge::ui::button("Create source",
                                          "Create Native/gameplay.cpp and CMakeLists.txt. Existing "
                                          "files are never overwritten."))
                        native.create_source();
                    if (forge::ui::button(
                            "Build & Reload",
                            "Incrementally compile, probe a unique candidate DLL, then reload at a "
                            "runtime boundary. Failed builds retain the previous module."))
                        native.build();
                    if (ImGui::InputText("CMake", cmake_path, sizeof(cmake_path))) {
                        native.cmake = cmake_path;
                        save_preferences();
                    }
                    forge::ui::help("CMake executable path or command on PATH. Version 3.24 or "
                                    "newer required.");
                    if (ImGui::InputText("Ninja", ninja_path, sizeof(ninja_path))) {
                        native.ninja = ninja_path;
                        save_preferences();
                    }
                    forge::ui::help(
                        "Ninja executable path or command on PATH. Launch FORGE from an x64 Native "
                        "Tools command prompt to provide the MSVC compiler environment.");
                    if (ImGui::Checkbox("Build on save", &auto_build)) {
                        native.auto_build = auto_build;
                        save_preferences();
                    }
                    forge::ui::help(
                        "Watch C/C++ headers, sources and CMake files in Native. Builds after "
                        "saves settle; play continues during compilation.");
                } catch (const std::exception& e) {
                    message = e.what();
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("%s", native.source_path().c_str());
                forge::ui::help("Edit this source in your code editor. The sample moves entities "
                                "along the X axis.");
                ImGui::TextWrapped("%s", native.status().c_str());
                forge::ui::help("Native build and candidate activation status.");
                ImGui::TextWrapped(
                    "Windows: use Run-Forge-Dev.cmd or an x64 Native Tools command prompt. "
                    "Requires Visual Studio C++ tools, CMake and Ninja.");
                forge::ui::help("These tools compile your gameplay locally. They are not needed to "
                                "open the editor.");
            }
            ImGui::End();
            if (ImGui::Begin("Console", nullptr, ImGuiWindowFlags_HorizontalScrollbar)) {
                forge::ui::heading("Status", "Latest editor operation or validation diagnostic.");
                ImGui::TextWrapped("%s", message.c_str());
                forge::ui::help("Latest operation result.");
                if (!native.log().empty()) {
                    forge::ui::heading("Build output",
                                       "Recent compiler output. The complete current log is in "
                                       "Project/.forge/native/build.log.");
                    ImGui::TextUnformatted(native.log().c_str());
                    forge::ui::help("Compiler diagnostics and build/validation results. Scroll "
                                    "horizontally for long source locations.");
                }
                if (!play.log().empty()) {
                    ImGui::TextWrapped("%s", play.log().c_str());
                    forge::ui::help(
                        "Recent runtime error output, limited to 64 KiB per play session.");
                }
                ImGui::TextWrapped("%s", play.status().c_str());
                forge::ui::help("Play process status. A runtime failure leaves the editor and "
                                "authored scene available.");
            }
            ImGui::End();
            auto* rtv = swap->GetCurrentBackBufferRTV();
            context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[] = {0.04f, 0.05f, 0.06f, 1};
            context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            gui->Render(context);
            swap->Present(1);
        }
        ImGui::SaveIniSettingsToDisk(ini.c_str());
        context->Flush();
        context->WaitForIdle();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    window.reset();
    SDL_Quit();
    return result;
}
