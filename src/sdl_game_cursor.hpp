#pragma once
#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>

namespace forge {
// Borrowed window, main-thread only. Destroy before the window. Capture is an
// explicit request; focus returning never silently recaptures the cursor.
class SdlGameCursor {
  public:
    explicit SdlGameCursor(SDL_Window* window) : window_(window) {
        if (!window_)
            throw std::invalid_argument("Game cursor requires a live window");
    }
    ~SdlGameCursor() { release(); }
    SdlGameCursor(const SdlGameCursor&) = delete;
    SdlGameCursor& operator=(const SdlGameCursor&) = delete;
    bool captured() const { return captured_; }
    void capture() {
        if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS))
            throw std::runtime_error("Click the game window before capturing the cursor");
        if (!SDL_SetWindowRelativeMouseMode(window_, true))
            throw std::runtime_error(std::string("Cursor capture failed: ") + SDL_GetError());
        captured_ = true;
    }
    void release() noexcept {
        if (captured_ && !SDL_SetWindowRelativeMouseMode(window_, false))
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cursor release failed: %s", SDL_GetError());
        captured_ = false;
    }
    bool event(const SDL_Event& event) {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
            event.window.windowID == SDL_GetWindowID(window_)) {
            release();
            return true;
        }
        return false;
    }

  private:
    SDL_Window* window_;
    bool captured_ = false;
};
} // namespace forge
