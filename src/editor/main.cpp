#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#include "ImGuiImplSDL3.hpp"
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
        try {
            std::ifstream f(settings);
            if (f) {
                forge::Json j;
                f >> j;
                forge::ui::tooltips = j.value("tooltips", true);
                forge::ui::style(j.value("interface_scale", 1.0f));
            }
        } catch (const std::exception&) { /* Recover malformed user preferences with defaults. */
        }
        auto save_preferences = [&] {
            forge::atomic_write(settings,
                                forge::Json{{"tooltips", forge::ui::tooltips},
                                            {"interface_scale", forge::ui::interface_scale}}
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
        std::string message =
            "Ready. Block preview is an authoring diagnostic, not the final game renderer.";
        if (std::filesystem::exists(scene_path))
            scene.load(scene_path);
        bool initialize_layout = !std::filesystem::exists(ini);
        forge::Viewport viewport(device);
        std::string selected;
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
                    if (forge::ui::button(
                            play.active() ? "Restart" : "Play",
                            "Start a fresh isolated play world from the current authored scene."))
                        play.start(runtime_path, scene.document());
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
                for (const auto& e : doc["entities"]) {
                    auto id = e.at("id").get<std::string>();
                    auto label = e.at("name").get<std::string>() + "##" + id;
                    if (ImGui::Selectable(label.c_str(), id == selected))
                        selected = id;
                    forge::ui::help(
                        "Select this entity to inspect and edit its authored properties.");
                }
            }
            ImGui::End();
            if (ImGui::Begin("Inspector")) {
                forge::ui::heading("Properties", "Position properties use world units. Changes are "
                                                 "undoable and serialize with the scene.");
                for (auto& e : doc["entities"])
                    if (e.at("id") == selected) {
                        ImGui::TextUnformatted(e.at("name").get_ref<const std::string&>().c_str());
                        forge::ui::help("Display name of the selected entity.");
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
            if (ImGui::Begin("Scene")) {
                forge::ui::heading("Block preview",
                                   "Diligent renders a cube for each entity with a position. "
                                   "Camera is fixed for this foundation build.");
                ImGui::TextUnformatted(play.active() ? "PLAY | isolated scene copy"
                                                     : "EDIT | authored scene");
                forge::ui::help(
                    "During play this preview shows runtime positions. Inspector edits "
                    "still affect authoring; Restart applies them to a fresh play world.");
                auto size = ImGui::GetContentRegionAvail();
                if (size.x > 1 && size.y > 1) {
                    auto* texture = viewport.render(context, play.active() ? play.snapshot() : doc,
                                                    unsigned(std::clamp(size.x, 1.0f, 4096.0f)),
                                                    unsigned(std::clamp(size.y, 1.0f, 4096.0f)));
                    ImGui::Image(ImTextureRef{reinterpret_cast<ImTextureID>(texture)}, size);
                    forge::ui::help("Authoring preview. Select an entity in World and edit its "
                                    "position in Inspector.");
                }
            }
            ImGui::End();
            if (ImGui::Begin("Console")) {
                forge::ui::heading("Status", "Latest editor operation or validation diagnostic.");
                ImGui::TextWrapped("%s", message.c_str());
                forge::ui::help("Latest operation result.");
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
