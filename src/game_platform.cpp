#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <forge/game_platform.hpp>
#include <memory>
#include <stdexcept>
namespace forge {
std::filesystem::path game_user_data_base() {
#if defined(__linux__)
    // The exact SDL Unix implementation assumes a nonempty directory environment
    // and does not enforce absolute XDG paths. Admit that input before calling it.
    const char* base = SDL_getenv("XDG_DATA_HOME");
    if (!base)
        base = SDL_getenv("HOME");
    if (!base || !*base || !std::filesystem::u8path(base).is_absolute())
        throw std::runtime_error(
            "game.storage: OS user-data environment must name an absolute directory");
#endif
    std::unique_ptr<char, decltype(&SDL_free)> path(SDL_GetPrefPath("FORGE", "Games"), SDL_free);
    if (!path)
        throw std::runtime_error(
            std::string("game.storage: Cannot locate OS user-data directory: ") + SDL_GetError());
    auto result = std::filesystem::u8path(path.get());
    if (!result.is_absolute() || !std::filesystem::is_directory(result))
        throw std::runtime_error(
            "game.storage: OS user-data location is not an absolute directory");
    return std::filesystem::canonical(result);
}
} // namespace forge
