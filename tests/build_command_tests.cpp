#include "editor/build_command.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool pump(BuildCommand& command, std::string& output, int& code) {
    const auto before = output.size();
    const auto done = command.pump(output, code);
    check(output.size() - before <= 32 * 8192, "Command output exceeded the per-pump byte bound");
    return done;
}
int finish(BuildCommand& command, std::string& output) {
    const auto deadline = SDL_GetTicks() + 8000;
    int code = 0;
    while (!pump(command, output, code)) {
        check(SDL_GetTicks() < deadline, "Native command did not finish");
        SDL_Delay(1);
    }
    check(command.retired() && !command.running(), "Completion did not confirm retirement");
    return code;
}
void ready(BuildCommand& command, const std::filesystem::path& root, std::string& output) {
    const auto deadline = SDL_GetTicks() + 5000;
    int code = 0;
    while (!std::filesystem::exists(root / "ready")) {
        check(!pump(command, output, code), "Command ended before fixture readiness");
        check(SDL_GetTicks() < deadline, "Command fixture readiness timed out");
        SDL_Delay(1);
    }
}
void no_writer(const std::filesystem::path& root) {
    std::ofstream(root / "retired") << "retired";
    SDL_Delay(150);
    check(!std::filesystem::exists(root / "late-write"), "A command writer survived retirement");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Python, process fixture and scratch parent");
        check(SDL_Init(0), "Cannot initialize native command fixture");
        struct SdlCleanup {
            ~SdlCleanup() { SDL_Quit(); }
        } sdl_cleanup;
        const auto root =
            std::filesystem::absolute(argv[3]) / ("run-" + std::to_string(SDL_GetTicksNS()));
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        BuildCommand command;
        auto args = [&](std::string mode, const std::filesystem::path& path) {
            return std::vector<std::string>{argv[1], argv[2], "--target", path_utf8(path), mode};
        };
        auto environment = std::unique_ptr<SDL_Environment, decltype(&SDL_DestroyEnvironment)>(
            SDL_CreateEnvironment(true), SDL_DestroyEnvironment);
        check(
            bool(environment) &&
                SDL_SetEnvironmentVariable(environment.get(), "PYTHONIOENCODING", "utf-8", true) &&
                SDL_SetEnvironmentVariable(environment.get(), "FORGE_COMMAND_FIXTURE", "café",
                                           true),
            "Cannot prepare command test environment");
        auto echo = args("echo", root);
        const std::vector<std::string> values{
            "",       "two words",      "日本語",         "\"quoted\"",
            "last\\", "slash\\\"quote", "$(literal) & %", std::string(20000, 'a')};
        echo.insert(echo.end(), values.begin(), values.end());
        std::string output;
        command.start(echo, environment.get(), root);
        check(finish(command, output) == 7, "Native command exit status changed");
        const auto line = output.substr(0, output.find('\n'));
        const auto echoed = nlohmann::json::parse(line);
        check(echoed.at("args") == values &&
                  std::filesystem::equivalent(
                      std::filesystem::u8path(echoed.at("cwd").get<std::string>()), root) &&
                  echoed.at("environment") == "café" && echoed.at("stdin") == "" &&
                  output.find("merged target stderr") != std::string::npos,
              "Native command argv/cwd/environment/output changed");
        for (const auto* mode : {"natural", "cancel", "close"}) {
            const auto path = root / mode;
            std::filesystem::create_directory(path);
            output.clear();
            command.start(args(std::string_view(mode) == "natural" ? "natural" : "escape", path));
            if (std::string_view(mode) == "natural")
                check(finish(command, output) == 7, "Natural command status changed");
            else {
                ready(command, path, output);
                if (std::string_view(mode) == "close")
                    check(command.close(), "Close returned without retiring writers");
                else {
                    command.cancel();
                    check(command.running(), "Cancel abandoned its owned supervisor");
                    check(finish(command, output) == 130, "Cancelled command status changed");
                }
            }
            no_writer(path);
        }
        {
            BuildCommand unrelated;
            const auto first = root / "concurrent-first", second = root / "concurrent-second";
            std::filesystem::create_directory(first);
            std::filesystem::create_directory(second);
            output.clear();
            std::string other_output;
            command.start(args("escape", first));
            unrelated.start(args("escape", second));
            ready(command, first, output);
            ready(unrelated, second, other_output);
            command.cancel();
            check(finish(command, output) == 130, "Concurrent cancellation failed");
            int other_code = 0;
            check(!pump(unrelated, other_output, other_code),
                  "Cancellation retired another command");
            unrelated.cancel();
            check(finish(unrelated, other_output) == 130, "Second command lost its ownership");
            no_writer(first);
            no_writer(second);
        }
        const auto timeout = root / "timeout";
        std::filesystem::create_directory(timeout);
        output.clear();
        command.start(args("output", timeout), nullptr, {}, 1000);
        ready(command, timeout, output);
        SDL_Delay(1100); // Let the target block in output before requesting retirement.
        bool rejected = false;
        try {
            (void)finish(command, output);
        } catch (const std::exception& error) {
            rejected = std::string(error.what()).find("1000 ms") != std::string::npos;
        }
        check(rejected && command.retired() && !command.running(),
              "Timeout reported completion before confirmed retirement");
        output.clear();
        command.start(echo, environment.get(), root);
        check(finish(command, output) == 7, "Command could not restart after timeout");
        std::cout
            << "Actual BuildCommand launch, arguments, cancel, close, timeout and retry passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
