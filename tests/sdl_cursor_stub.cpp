// Stub implementation of SDL_SetWindowRelativeMouseMode,
// SDL_GetWindowRelativeMouseMode, SDL_GetWindowFlags, SDL_GetError,
// SDL_GetWindowID, SDL_LogWarn used by the forge_cursor_truthful_tests
// shim.
//
// Linked directly into the test executable so the real SDL3 shared
// library is not required. The test process overrides the atomic globals
// declared here to inject each outcome.
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstring>

// Setter outcome (what SDL_SetWindowRelativeMouseMode reports).
std::atomic<int> g_set_calls{0};
std::atomic<bool> g_set_returns_true{true};
std::atomic<bool> g_set_records_intent{true};
// Getter outcome (what SDL_GetWindowRelativeMouseMode reports).
std::atomic<bool> g_get_returns_true{false};
// Window flag (SDL_WindowFlags). 0 means no flags.
std::atomic<Uint64> g_window_flags{SDL_WINDOW_INPUT_FOCUS};
// Cached error string.
static char g_error_buf[256] = {0};
// Cached window ID echoed by SDL_GetWindowID. External linkage so the
// test translation unit can override it before injecting scenarios.
std::atomic<Uint32> g_window_id{1};

extern "C" {

bool SDLCALL SDL_SetWindowRelativeMouseMode(SDL_Window*, bool enabled) {
    g_set_calls.fetch_add(1);
    if (g_set_records_intent.load())
        g_get_returns_true.store(enabled);
    return g_set_returns_true.load();
}

bool SDLCALL SDL_GetWindowRelativeMouseMode(SDL_Window*) { return g_get_returns_true.load(); }

SDL_WindowFlags SDLCALL SDL_GetWindowFlags(SDL_Window*) { return g_window_flags.load(); }

const char* SDLCALL SDL_GetError(void) { return g_error_buf[0] ? g_error_buf : "stub-error"; }

Uint32 SDLCALL SDL_GetWindowID(SDL_Window*) { return g_window_id.load(); }

void SDLCALL SDL_LogWarn(int, const char*, ...) {
    // no-op for tests
}

} // extern "C"
