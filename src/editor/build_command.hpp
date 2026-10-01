#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <forge/project_paths.hpp>
#include <stdexcept>
#include <string>
#include <vector>
namespace forge {
// Compiler output never shares the runtime's JSON protocol pipe.
class BuildCommand {
  public:
    ~BuildCommand() { close(); }
    BuildCommand() = default;
    BuildCommand(const BuildCommand&) = delete;
    BuildCommand& operator=(const BuildCommand&) = delete;
    void close() {
        if (process_) {
            SDL_KillProcess(process_, true);
            SDL_WaitProcess(process_, true, nullptr);
            SDL_DestroyProcess(process_);
            process_ = nullptr;
        }
    }
    void start(const std::vector<std::string>& arguments, SDL_Environment* environment = nullptr,
               const std::filesystem::path& directory = {}) {
        close();
        std::vector<const char*> args;
        for (const auto& argument : arguments)
            args.push_back(argument.c_str());
        args.push_back(nullptr);
        const auto properties = SDL_CreateProperties();
        if (!properties)
            throw std::runtime_error(SDL_GetError());
        const auto working_directory = path_utf8(directory);
        const bool configured =
            (directory.empty() ||
             SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                                   working_directory.c_str())) &&
            (!environment ||
             SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER,
                                    environment)) &&
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data()) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
                                   true);
        if (configured)
            process_ = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (!process_)
            throw std::runtime_error(std::string("Cannot start build tool: ") + SDL_GetError());
        started_ = SDL_GetTicks();
    }
    // Returns true on completion; output is bounded per editor frame.
    bool pump(std::string& output, int& exit_code) {
        char buffer[8192];
        for (int i = 0; i < 32; ++i) {
            const auto count = SDL_ReadIO(SDL_GetProcessOutput(process_), buffer, sizeof(buffer));
            if (!count)
                break;
            output.append(buffer, count);
        }
        if (SDL_GetTicks() - started_ > 180000) {
            close();
            throw std::runtime_error("Native build timed out after three minutes");
        }
        if (!SDL_WaitProcess(process_, false, &exit_code))
            return false;
        // Drain trailing output after process exit; each frame remains bounded.
        const auto count = SDL_ReadIO(SDL_GetProcessOutput(process_), buffer, sizeof(buffer));
        if (count) {
            output.append(buffer, count);
            return false;
        }
        SDL_DestroyProcess(process_);
        process_ = nullptr;
        return true;
    }

  private:
    SDL_Process* process_ = nullptr;
    Uint64 started_ = 0;
};

} // namespace forge
