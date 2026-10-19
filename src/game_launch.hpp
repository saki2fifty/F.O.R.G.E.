#pragma once
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
namespace forge {
struct GameLaunch {
    std::optional<std::string> project;
    bool verify_startup = false, show_fps = false;
};
inline GameLaunch parse_game_launch(std::span<const std::string_view> arguments) {
    GameLaunch result;
    const auto usage = [] {
        throw std::runtime_error("Usage: forge_game [--project <development directory> | "
                                 "--verify-startup] [--show-fps]");
    };
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto argument = arguments[i];
        if (argument == "--show-fps")
            result.show_fps = true;
        else if (argument == "--verify-startup")
            result.verify_startup = true;
        else if (argument == "--project") {
            if (result.project || ++i == arguments.size() || arguments[i].empty() ||
                arguments[i].starts_with("--"))
                usage();
            result.project = std::string(arguments[i]);
        } else
            usage();
    }
    if (result.project && result.verify_startup)
        usage();
    return result;
}
} // namespace forge
