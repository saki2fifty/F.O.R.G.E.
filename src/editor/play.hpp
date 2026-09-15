#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <forge/scene.hpp>
#include <string>

namespace forge {
// One bounded request in flight. SDL process pipes are nonblocking; pump never waits.
// This controller owns no gameplay code and never mutates the authoring Scene.
class PlaySession {
  public:
    ~PlaySession() { stop(); }
    PlaySession() = default;
    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;
    bool active() const { return process_ != nullptr; }
    const Json& snapshot() const { return snapshot_; }
    const std::string& status() const { return status_; }
    const std::string& log() const { return log_; }
    void stop() {
        if (process_) {
            SDL_KillProcess(process_, true);
            SDL_WaitProcess(process_, true, nullptr);
            SDL_DestroyProcess(process_);
            process_ = nullptr;
        }
        outgoing_.clear();
        incoming_.clear();
        waiting_ = false;
        status_ = "Stopped. Authored scene preserved.";
    }
    void start(const std::string& executable, const Json& scene) {
        stop();
        snapshot_ = scene;
        const char* args[] = {executable.c_str(), nullptr};
        log_.clear();
        const auto properties = SDL_CreateProperties();
        if (!properties) {
            status_ = SDL_GetError();
            return;
        }
        const bool configured =
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
        if (!process_) {
            status_ = std::string("Cannot start play: ") + SDL_GetError();
            return;
        }
        status_ = "Starting play...";
        last_step_ = SDL_GetTicks();
        try {
            send({{"command", "replace"}, {"scene", scene}});
        } catch (const std::exception& error) {
            stop();
            status_ = error.what();
        }
    }
    void pump() {
        if (!process_)
            return;
        try {
            if (!outgoing_.empty()) {
                auto* input = SDL_GetProcessInput(process_);
                const auto count = SDL_WriteIO(input, outgoing_.data(), outgoing_.size());
                outgoing_.erase(0, count);
                if (!count && SDL_GetIOStatus(input) != SDL_IO_STATUS_NOT_READY)
                    throw std::runtime_error("Runtime input closed");
            }
            char buffer[8192];
            auto* output = SDL_GetProcessOutput(process_);
            // Bound work per frame, including when a faulty runtime floods stdout.
            for (int i = 0; i < 32; ++i) {
                const auto count = SDL_ReadIO(output, buffer, sizeof(buffer));
                if (!count)
                    break;
                incoming_.append(buffer, count);
                if (incoming_.size() > 16 * 1024 * 1024)
                    throw std::runtime_error("Runtime response exceeds 16 MiB");
            }
            auto* errors = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(
                SDL_GetProcessProperties(process_), SDL_PROP_PROCESS_STDERR_POINTER, nullptr));
            if (errors) {
                for (int i = 0; i < 8; ++i) {
                    const auto count = SDL_ReadIO(errors, buffer, sizeof(buffer));
                    if (!count)
                        break;
                    log_.append(buffer, count);
                    if (log_.size() > 65536)
                        log_.erase(0, log_.size() - 65536);
                }
            }
            const auto newline = incoming_.find('\n');
            if (newline != std::string::npos) {
                const auto response = Json::parse(incoming_.substr(0, newline));
                incoming_.erase(0, newline + 1);
                if (response.value("protocol", 0) != 1 || !response.value("ok", false))
                    throw std::runtime_error(response.value("error", "Invalid runtime response"));
                snapshot_ = response.at("scene");
                waiting_ = false;
                status_ = "Playing in isolated runtime. Stop returns to the authored scene.";
            }
            int exit_code = 0;
            if (SDL_WaitProcess(process_, false, &exit_code))
                throw std::runtime_error("Runtime exited (code " + std::to_string(exit_code) + ")");
            if (waiting_ && SDL_GetTicks() - sent_at_ > 5000)
                throw std::runtime_error("Runtime timed out");
            if (!waiting_) {
                const auto now = SDL_GetTicks();
                const float seconds = std::clamp(float(now - last_step_) / 1000.0f, 0.0f, 0.1f);
                last_step_ = now;
                send({{"command", "step"}, {"seconds", seconds}});
            }
        } catch (const std::exception& error) {
            const std::string diagnostic = error.what();
            stop();
            status_ = diagnostic + ". Authored scene is safe; press Play to restart.";
        }
    }

  private:
    void send(Json request) {
        request["protocol"] = 1;
        outgoing_ = request.dump() + "\n";
        if (outgoing_.size() > 16 * 1024 * 1024)
            throw std::runtime_error("Play scene exceeds 16 MiB transport limit");
        waiting_ = true;
        sent_at_ = SDL_GetTicks();
    }
    SDL_Process* process_ = nullptr;
    Json snapshot_;
    std::string outgoing_, incoming_, log_;
    std::string status_ = "Stopped. Play uses a copy of your authored scene.";
    bool waiting_ = false;
    Uint64 sent_at_ = 0;
    Uint64 last_step_ = 0;
};
} // namespace forge
