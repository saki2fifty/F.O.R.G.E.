#pragma once
#include <SDL3/SDL.h>
#include <string>
namespace forge {
inline std::string sdl_key_control(SDL_Scancode key) {
    if (key >= SDL_SCANCODE_A && key <= SDL_SCANCODE_Z)
        return std::string("key.") + char('a' + key - SDL_SCANCODE_A);
    if (key >= SDL_SCANCODE_1 && key <= SDL_SCANCODE_9)
        return std::string("key.") + char('1' + key - SDL_SCANCODE_1);
    switch (key) {
#define FORGE_KEY(sdl, id)                                                                         \
    case SDL_SCANCODE_##sdl:                                                                       \
        return "key." id
        FORGE_KEY(0, "0");
        FORGE_KEY(SPACE, "space");
        FORGE_KEY(LEFT, "left");
        FORGE_KEY(RIGHT, "right");
        FORGE_KEY(UP, "up");
        FORGE_KEY(DOWN, "down");
        FORGE_KEY(LSHIFT, "lshift");
        FORGE_KEY(RSHIFT, "rshift");
        FORGE_KEY(LCTRL, "lctrl");
        FORGE_KEY(RCTRL, "rctrl");
        FORGE_KEY(LALT, "lalt");
        FORGE_KEY(RALT, "ralt");
        FORGE_KEY(TAB, "tab");
        FORGE_KEY(RETURN, "enter");
        FORGE_KEY(BACKSPACE, "backspace");
#undef FORGE_KEY
    default:
        return {};
    }
}
} // namespace forge
