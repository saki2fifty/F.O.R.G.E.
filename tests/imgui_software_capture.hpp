#pragma once
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <filesystem>
#include <forge/project_paths.hpp>
#include <imgui.h>
#include <memory>
#include <stdexcept>

namespace forge::test {
// Actual ImGui draw data rendered through SDL's software backend for manual captures.
class ImGuiSoftwareCapture {
    struct Backend {
        explicit Backend(SDL_Renderer* renderer) {
            if (!ImGui_ImplSDLRenderer3_Init(renderer))
                throw std::runtime_error("UI capture backend unavailable");
        }
        ~Backend() { ImGui_ImplSDLRenderer3_Shutdown(); }
    };
    static SDL_Renderer* renderer(SDL_Surface* surface) {
        if (!surface)
            throw std::runtime_error(SDL_GetError());
        auto* result = SDL_CreateSoftwareRenderer(surface);
        if (!result)
            throw std::runtime_error(SDL_GetError());
        return result;
    }
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface_;
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer_;
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context_;
    Backend backend_;

  public:
    ImGuiSoftwareCapture(int width, int height)
        : surface_(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface),
          renderer_(renderer(surface_.get()), SDL_DestroyRenderer),
          context_(ImGui::CreateContext(), ImGui::DestroyContext), backend_(renderer_.get()) {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {float(width), float(height)};
        io.DeltaTime = 1.f / 60;
        const auto font = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "resources/ui/LatoLatin-Regular.ttf";
        if (!io.Fonts->AddFontFromFileTTF(path_utf8(font).c_str(), 16))
            throw std::runtime_error("UI capture font unavailable");
    }
    void new_frame() {
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
    }
    SDL_Renderer* native_renderer() const { return renderer_.get(); }
    void render() {
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer_.get(), 9, 15, 18, 255);
        SDL_RenderClear(renderer_.get());
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_.get());
        SDL_RenderPresent(renderer_.get());
    }
    void save(const std::filesystem::path& path) const {
        if (!SDL_SavePNG(surface_.get(), path_utf8(path).c_str()))
            throw std::runtime_error(SDL_GetError());
    }
};
} // namespace forge::test
