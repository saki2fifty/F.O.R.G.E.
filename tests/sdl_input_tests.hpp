#pragma once
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <stdexcept>

// Inject events through SDL's queue and the production platform backend.
// This checks translation/filtering, not physical devices or desktop focus policy.
inline void test_sdl_input_backend() {
    auto check = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    };
#ifndef _WIN32
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#endif
    check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    auto* window = SDL_CreateWindow("FORGE input regression", 640, 400, SDL_WINDOW_HIDDEN);
    check(window != nullptr, SDL_GetError());
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {640, 400};
    io.DeltaTime = 1.0f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
#ifdef _WIN32
    check(ImGui_ImplSDL3_InitForD3D(window), "SDL3 D3D input backend failed");
#else
    check(ImGui_ImplSDL3_InitForOther(window), "SDL3 input backend failed");
#endif
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
    }
    const auto id = SDL_GetWindowID(window);
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = id + 1;
    event.key.key = SDLK_MINUS;
    event.key.scancode = SDL_SCANCODE_MINUS;
    event.key.mod = SDL_KMOD_CTRL;
    event.key.down = true;
    check(!ImGui_ImplSDL3_ProcessEvent(&event), "Accepted another window's keyboard event");
    event.key.windowID = id;
    check(SDL_PushEvent(&event), "SDL key event enqueue failed");
    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = id;
    event.button.button = SDL_BUTTON_RIGHT;
    event.button.down = true;
    check(SDL_PushEvent(&event), "SDL mouse event enqueue failed");
    while (SDL_PollEvent(&event))
        ImGui_ImplSDL3_ProcessEvent(&event);
    ImGui::NewFrame();
    check(io.KeyCtrl && ImGui::IsKeyDown(ImGuiKey_Minus), "Ctrl-minus translation failed");
    check(ImGui::IsMouseDown(ImGuiMouseButton_Right), "RMB translation failed");
    ImGui::EndFrame();
    event = {};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = id;
    check(ImGui_ImplSDL3_ProcessEvent(&event), "SDL focus-loss event rejected");
    ImGui::NewFrame();
    check(!io.KeyCtrl && !ImGui::IsKeyDown(ImGuiKey_Minus) &&
              !ImGui::IsMouseDown(ImGuiMouseButton_Right),
          "Focus loss left input held");
    ImGui::EndFrame();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyWindow(window);
    SDL_Quit();
}
