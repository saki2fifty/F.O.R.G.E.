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
    enum class Reload { Idle, Pending, Succeeded, Failed };
    bool active() const { return process_ != nullptr; }
    bool ready() const { return active() && stage_ == Stage::Running; }
    const std::string& module() const { return module_; }
    Reload reload_result() const { return reload_result_; }
    bool can_recover() const { return !active() && recoverable_; }
    void recover() {
        if (can_recover()) {
            const auto saved = snapshot_;
            start(executable_, saved, module_);
        }
    }
    // Caller must probe this immutable artifact in a separate runtime first.
    void reload(const std::string& path) {
        if (!ready() || reload_result_ == Reload::Pending)
            throw std::runtime_error("Play is not ready for reload");
        requested_ = path;
        reload_result_ = Reload::Pending;
    }
    const Json& snapshot() const { return snapshot_; }
    const Json& effective_snapshot() const { return effective_; }
    std::uint64_t snapshot_version() const { return snapshot_version_; }
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
        requested_.clear();
        recoverable_ = false;
        status_ = "Stopped. Authored scene preserved.";
    }
    void start(const std::string& executable, const Json& scene, const std::string& module = {},
               bool probe = false) {
        // Copy arguments before stop/launch; recover may pass our own members.
        executable_ = executable;
        const auto initial = scene;
        loading_ = module;
        module_ = module;
        probe_ = probe;
        transaction_ = false;
        restoring_ = false;
        reload_result_ = Reload::Idle;
        notice_.clear();
        launch(initial);
    }

  private:
    enum class Stage { Replace, Load, Validate, Running };
    void launch(const Json& scene) {
        stop();
        snapshot_ = scene;
        effective_ = scene;
        ++snapshot_version_;
        stage_ = Stage::Replace;
        const auto& executable = executable_;
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

  public:
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
                if (response.value("protocol", 0) != 1)
                    throw std::runtime_error("Invalid runtime response protocol");
                if (!response.value("ok", false)) {
                    const auto error = response.value("error", "Runtime rejected request");
                    if (transaction_ && stage_ == Stage::Load &&
                        error.starts_with("Play restart required:")) {
                        notice_ =
                            "Schema changed; restarted play with compatible host-owned values.";
                        launch(checkpoint_);
                        return;
                    }
                    throw std::runtime_error(error);
                }
                snapshot_ = response.at("scene");
                effective_ = response.at("effective_scene");
                ++snapshot_version_;
                waiting_ = false;
                if (stage_ == Stage::Replace) {
                    if (!loading_.empty()) {
                        stage_ = Stage::Load;
                        send({{"command", "load_module"}, {"path", loading_}});
                    } else {
                        stage_ = Stage::Validate;
                        send({{"command", "step"}, {"seconds", 0}});
                    }
                } else if (stage_ == Stage::Load) {
                    stage_ = Stage::Validate;
                    send({{"command", "step"}, {"seconds", 0}});
                } else if (stage_ == Stage::Validate) {
                    stage_ = Stage::Running;
                    module_ = loading_;
                    if (transaction_) {
                        transaction_ = false;
                        reload_result_ = Reload::Succeeded;
                        notice_ = "Reload committed. " + notice_;
                    }
                    restoring_ = false;
                    status_ = "Playing in isolated runtime. " + notice_;
                }
            }
            int exit_code = 0;
            if (SDL_WaitProcess(process_, false, &exit_code))
                throw std::runtime_error("Runtime exited (code " + std::to_string(exit_code) + ")");
            if (waiting_ && SDL_GetTicks() - sent_at_ > 5000)
                throw std::runtime_error("Runtime timed out");
            if (!waiting_ && stage_ == Stage::Running) {
                if (!requested_.empty()) {
                    checkpoint_ = snapshot_;
                    previous_ = module_;
                    loading_ = requested_;
                    requested_.clear();
                    transaction_ = true;
                    notice_.clear();
                    stage_ = Stage::Load;
                    send({{"command", "load_module"}, {"path", loading_}});
                } else if (!probe_) {
                    const auto now = SDL_GetTicks();
                    const float seconds = std::clamp(float(now - last_step_) / 1000.0f, 0.0f, 0.1f);
                    last_step_ = now;
                    send({{"command", "step"}, {"seconds", seconds}});
                }
            }
        } catch (const std::exception& error) {
            const std::string diagnostic = error.what();
            if (transaction_) {
                transaction_ = false;
                reload_result_ = Reload::Failed;
                loading_ = previous_;
                module_ = previous_;
                restoring_ = true;
                notice_ = "Reload failed; restored previous module and checkpoint: " + diagnostic;
                launch(checkpoint_);
            } else {
                const bool may_recover = !probe_ && !restoring_ && stage_ == Stage::Running;
                stop();
                recoverable_ = may_recover;
                status_ = diagnostic + ". Authored scene is safe; " +
                          (may_recover ? "Recover resumes the last completed checkpoint."
                                       : "press Play to restart.");
            }
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
    Stage stage_ = Stage::Replace;
    Reload reload_result_ = Reload::Idle;
    std::string executable_, module_, loading_, requested_, previous_, notice_;
    Json checkpoint_;
    bool transaction_ = false, restoring_ = false, probe_ = false, recoverable_ = false;
    Json effective_;
    Json snapshot_;
    std::uint64_t snapshot_version_ = 0;
    std::string outgoing_, incoming_, log_;
    std::string status_ = "Stopped. Play uses a copy of your authored scene.";
    bool waiting_ = false;
    Uint64 sent_at_ = 0;
    Uint64 last_step_ = 0;
};
} // namespace forge
