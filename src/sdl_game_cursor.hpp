#pragma once
#include <SDL3/SDL.h>
#include <stdexcept>
#include <string>

namespace forge {
// Borrowed window, main-thread only. Destroy before the window. Capture is an
// explicit request; focus returning never silently recaptures the cursor.
//
// SDL_GetWindowRelativeMouseMode on the borrowed window reports SDL's
// stored window relative-mouse flag — it is SDL's view of its own setter
// state, not an independent raw OS probe. Both captured() and relative()
// read this flag, so the SDL getter is the single source of truth. There
// is no cached intent flag; the destructor / focus-loss path is the only
// place that needs to remember a release is owed, and it consults the
// SDL getter directly.
class SdlGameCursor {
  public:
    explicit SdlGameCursor(SDL_Window* window) : window_(window) {
        if (!window_)
            throw std::invalid_argument("Game cursor requires a live window");
    }
    ~SdlGameCursor() noexcept { release(); }
    SdlGameCursor(const SdlGameCursor&) = delete;
    SdlGameCursor& operator=(const SdlGameCursor&) = delete;
    // SDL window relative-mode getter. The setter is validated against this
    // getter before any capture call returns, so a later SDL-side mode change
    // (focus restore, another viewport, driver quirk) is reflected here.
    bool captured() const { return SDL_GetWindowRelativeMouseMode(window_); }
    bool relative() const { return SDL_GetWindowRelativeMouseMode(window_); }
    void capture() {
        if (!(SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS))
            throw std::runtime_error("Click the game window before capturing the cursor");
        if (!SDL_SetWindowRelativeMouseMode(window_, true)) {
            // Operation failure: surface the SDL diagnostic. The getter
            // can still legitimately report false (or true, if another
            // viewport changed it); callers consult relative().
            throw std::runtime_error(std::string("Cursor capture failed: ") + SDL_GetError());
        }
        if (!SDL_GetWindowRelativeMouseMode(window_))
            throw std::runtime_error(
                "Cursor capture reported success but relative mouse mode is off");
    }
    // Returns true only when the SDL window relative-mode flag is off after
    // the attempted release. Skips the setter call when the flag already
    // reads off, and reports failure when the setter rejects the request
    // or the getter still reports relative afterwards. The `captured()` /
    // `relative()` accessors remain truthful because they read the same
    // SDL window flag directly.
    bool checked_release() {
        const bool relative_on = SDL_GetWindowRelativeMouseMode(window_);
        if (!relative_on)
            return true;
        if (!SDL_SetWindowRelativeMouseMode(window_, false)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cursor release failed: %s", SDL_GetError());
            return false;
        }
        if (SDL_GetWindowRelativeMouseMode(window_)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cursor release left window relative");
            return false;
        }
        return true;
    }
    // Best-effort destructor / focus-loss path. noexcept: never throws.
    void release() noexcept {
        if (!checked_release())
            SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cursor release could not confirm mode off");
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
};
} // namespace forge