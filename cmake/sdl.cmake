include_guard(GLOBAL)
FetchContent_Declare(sdl GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG fa2c02bb6e21974a89ea9824bc53c9932abe5f9c) # release-3.4.16
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
if(NOT FORGE_BUILD_EDITOR)
 # Process-controller tests need SDL processes/IO/time, not a Linux desktop stack.
 set(SDL_VIDEO OFF)
 set(SDL_AUDIO OFF)
 set(SDL_JOYSTICK OFF)
 set(SDL_HAPTIC OFF)
 set(SDL_UNIX_CONSOLE_BUILD ON)
endif()
FetchContent_MakeAvailable(sdl)
