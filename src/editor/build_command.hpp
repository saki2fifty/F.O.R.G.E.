#pragma once
#include "../bounded_json.hpp"
#include "../self_executable.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <forge/project_paths.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace forge {
// Existing task owner; private helper owns descendants. Logs and the retirement
// receipt have separate pipes, neither of which shares the runtime JSON protocol.
class BuildCommand {
  public:
    ~BuildCommand() { close(); }
    BuildCommand() = default;
    BuildCommand(const BuildCommand&) = delete;
    BuildCommand& operator=(const BuildCommand&) = delete;
    bool retired() const { return retired_; }
    bool running() const { return process_ != nullptr; }
    void cancel() noexcept {
        cancel_requested_ = true;
        if (process_) {
            auto* input = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
                SDL_GetProcessProperties(process_), SDL_PROP_PROCESS_STDIN_POINTER, nullptr));
            if (input)
                SDL_CloseIO(input); // EOF reaches the helper even during a partial launch.
        }
        launch_.clear();
    }
    bool close() noexcept {
        if (!process_)
            return retired_;
        cancel();
        while (process_) {
            try {
                std::string discard;
                int code = 0;
                (void)pump(discard, code);
            } catch (...) {
                if (process_) {
                    // Even if allocation/IO fails, EOF lets the independent
                    // supervisor join its writers. No receipt means no cleanup.
                    SDL_WaitProcess(process_, true, nullptr);
                    SDL_DestroyProcess(process_);
                    process_ = nullptr;
                    retired_ = false;
                }
            }
            if (process_)
                SDL_Delay(1);
        }
        return retired_;
    }
    void start(const std::vector<std::string>& arguments, SDL_Environment* environment = nullptr,
               const std::filesystem::path& directory = {}, Uint64 timeout_ms = 180000) {
        close();
        if (arguments.empty() || arguments.size() > 1024 || arguments.front().empty())
            throw std::runtime_error("Invalid native command arguments");
        for (const auto& argument : arguments)
            if (argument.find('\0') != std::string::npos)
                throw std::runtime_error("Native command argument contains NUL");
        const auto request = nlohmann::json{{"version", 1}, {"args", arguments}}.dump();
        if (request.size() > 1024 * 1024)
            throw std::runtime_error("Native command launch record exceeds its byte limit");
        auto helper = self_executable().parent_path() / "forge_tools";
#ifdef _WIN32
        helper += ".exe";
#endif
        if (!std::filesystem::is_regular_file(helper))
            throw std::runtime_error("Packaged FORGE command supervisor is missing");
        const auto executable = path_utf8(helper);
        const char* args[]{executable.c_str(), "--command-worker", nullptr};
        launch_.assign(4, '\0');
        for (unsigned i = 0; i < 4; ++i)
            launch_[i] = char((request.size() >> (24 - i * 8)) & 255);
        launch_ += request;
        written_ = 0;
        receipt_.clear();
        error_.clear();
        cancel_requested_ = false;
        receipt_invalid_ = false;
        helper_exited_ = false;
        retired_ = true; // No child has been created for this new command yet.
        exit_code_ = 125;
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
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                                  SDL_PROCESS_STDIO_APP);
        if (configured)
            process_ = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (!process_)
            throw std::runtime_error(std::string("Cannot start command supervisor: ") +
                                     SDL_GetError());
        retired_ = false;
        started_ = SDL_GetTicks();
        timeout_ms_ = timeout_ms;
    }
    // Returns true only after supervisor exit and a validated retirement receipt.
    // Cancellation/timeouts request retirement; pumping remains bounded per frame.
    bool pump(std::string& output, int& exit_code) {
        if (!process_) {
            exit_code = exit_code_;
            return true;
        }
        if (timeout_ms_ && SDL_GetTicks() - started_ > timeout_ms_ && !cancel_requested_) {
            error_ = "Native command timed out after " + std::to_string(timeout_ms_) + " ms";
            cancel();
        }
        if (!launch_.empty() && !cancel_requested_) {
            auto* input = SDL_GetProcessInput(process_);
            const auto count =
                input ? SDL_WriteIO(input, launch_.data() + written_,
                                    std::min<std::size_t>(8192, launch_.size() - written_))
                      : 0;
            written_ += count;
            if (written_ == launch_.size())
                launch_.clear();
            else if (!input || (!count && SDL_GetIOStatus(input) == SDL_IO_STATUS_ERROR)) {
                error_ = "Cannot send command launch record";
                cancel();
            }
        }
        char buffer[8192];
        bool output_pending = false;
        for (int i = 0; i < 32; ++i) {
            const auto count = SDL_ReadIO(SDL_GetProcessOutput(process_), buffer, sizeof(buffer));
            if (!count)
                break;
            output.append(buffer, count);
            output_pending = i == 31;
        }
        auto* channel = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
            SDL_GetProcessProperties(process_), SDL_PROP_PROCESS_STDERR_POINTER, nullptr));
        const auto count = channel ? SDL_ReadIO(channel, buffer, sizeof(buffer)) : 0;
        if (count) {
            if (receipt_.size() + count <= 8192)
                receipt_.append(buffer, count);
            else {
                error_ = "Command supervisor receipt exceeded its byte limit";
                cancel();
                receipt_.clear();
                receipt_invalid_ = true;
            }
        }
        if (!helper_exited_) {
            helper_exited_ = SDL_WaitProcess(process_, false, &helper_code_);
            // The helper may have written its final bytes between this frame's
            // pipe reads and the wait. Drain again after observing its exit.
            return false;
        }
        if (output_pending || count)
            return false;
        SDL_DestroyProcess(process_);
        process_ = nullptr;
        try {
            const auto value =
                asset_detail::parse_bounded_json(std::as_bytes(std::span(receipt_)), 8192, 64, 4);
            if (receipt_invalid_ || !value.is_object() || value.size() != 5 ||
                !value.at("version").is_number_integer() || value.at("version") != 1 ||
                !value.at("retired").is_boolean() || !value.at("exit_code").is_number_integer() ||
                !value.at("cancelled").is_boolean() || !value.at("error").is_string())
                throw std::runtime_error("Invalid command supervisor receipt");
            const auto& result_code = value.at("exit_code");
            if (result_code.is_number_unsigned()
                    ? result_code.get<std::uint64_t>() >
                          std::uint64_t(std::numeric_limits<int>::max())
                    : result_code.get<std::int64_t>() < std::numeric_limits<int>::min() ||
                          result_code.get<std::int64_t>() > std::numeric_limits<int>::max())
                throw std::runtime_error("Invalid command exit status");
            retired_ = value.at("retired").get<bool>();
            exit_code_ = value.at("exit_code").get<int>();
            if (error_.empty())
                error_ = value.at("error").get<std::string>();
        } catch (...) {
            retired_ = false;
        }
        if (!retired_)
            throw std::runtime_error("Command retirement was not confirmed; its working files "
                                     "must be retained");
        if (!error_.empty())
            throw std::runtime_error(error_);
        if (helper_code_)
            throw std::runtime_error("Command supervisor failed after retirement");
        exit_code = exit_code_;
        return true;
    }

  private:
    SDL_Process* process_ = nullptr;
    Uint64 started_ = 0;
    Uint64 timeout_ms_ = 180000;
    std::string launch_, receipt_, error_;
    std::size_t written_ = 0;
    bool retired_ = true, cancel_requested_ = false, receipt_invalid_ = false;
    bool helper_exited_ = false;
    int helper_code_ = 0;
    int exit_code_ = 0;
};

} // namespace forge
