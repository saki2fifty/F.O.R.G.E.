#pragma once
#include <SDL3/SDL.h>
#include <forge/scene.hpp>
#include <string>
namespace forge {
// One correlated request in flight. Transport pumps never determine simulation dt.
// Pending activation retains only artifact/checkpoint, never a second loaded DLL.
class PlaySession {
  public:
    ~PlaySession() { stop(); }
    PlaySession() = default;
    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;
    enum class Reload { Idle, Pending, Succeeded, Failed, Cancelled };
    bool active() const { return process_ != nullptr; }
    bool ready() const { return active() && stage_ == Stage::Running; }
    bool paused() const { return timing_.value("paused", true); }
    bool pending_activation() const { return ready() && transaction_; }
    bool awaiting_activation_input() const {
        return pending_activation() && desired_paused_ && paused();
    }
    bool control_ready() const {
        return ready() && control_.empty() && (!waiting_ || sent_command_ == "snapshot");
    }
    const Json& timing() const { return timing_; }
    const std::string& session() const { return session_; }
    const std::string& module() const { return module_; } // Last known-good artifact only.
    Reload reload_result() const { return reload_result_; }
    bool can_recover() const { return !active() && recoverable_; }
    void pause() {
        if (control_ready())
            control_ = "pause";
    }
    void resume() {
        if (control_ready())
            control_ = "resume";
    }
    void step() {
        if (control_ready() && paused())
            control_ = "step";
    }
    void recover() {
        if (!can_recover())
            return;
        loading_ = module_;
        transaction_ = false;
        desired_paused_ = paused();
        restoring_ = true;
        launch(snapshot_);
    }
    // Caller has executed this immutable artifact in a separate fixed-tick probe.
    void reload(const std::string& path) {
        if (!ready())
            throw std::runtime_error("Play is not ready for reload");
        if (transaction_) {
            // No candidate tick completed: discard its world before superseding.
            // Keep the original known-good artifact/checkpoint and prior run policy.
            loading_ = path;
            notice_ = "Previous pending activation cancelled. ";
            launch(checkpoint_);
        } else {
            requested_ = path;
        }
        reload_result_ = Reload::Pending;
    }
    const Json& snapshot() const { return snapshot_; }
    const Json& effective_snapshot() const { return effective_; }
    std::uint64_t snapshot_version() const { return snapshot_version_; }
    const std::string& status() const { return status_; }
    const std::string& log() const { return log_; }
    void stop() {
        close_process();
        if (transaction_ || !requested_.empty())
            reload_result_ = Reload::Cancelled;
        transaction_ = false;
        requested_.clear();
        recoverable_ = false;
        status_ = "Stopped. Authored scene preserved.";
    }
    void start(const std::string& executable, const Json& scene, const std::string& module = {},
               bool probe = false) {
        const auto initial = scene;
        stop();
        executable_ = executable;
        loading_ = module;
        module_.clear();
        previous_.clear();
        checkpoint_ = initial;
        probe_ = probe;
        desired_paused_ = probe;
        prior_paused_ = probe;
        transaction_ = !module.empty();
        restoring_ = false;
        reload_result_ = Reload::Idle;
        notice_.clear();
        launch(initial);
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
            if (errors)
                for (int i = 0; i < 8; ++i) {
                    const auto count = SDL_ReadIO(errors, buffer, sizeof(buffer));
                    if (!count)
                        break;
                    log_.append(buffer, count);
                    if (log_.size() > 65536)
                        log_.erase(0, log_.size() - 65536);
                }
            const auto newline = incoming_.find('\n');
            if (newline != std::string::npos) {
                const auto response = Json::parse(incoming_.substr(0, newline));
                incoming_.erase(0, newline + 1);
                if (!waiting_ || response.value("protocol", 0) != 2 ||
                    response.value("id", std::uint64_t{}) != request_id_)
                    throw std::runtime_error("Invalid/stale runtime response");
                if (stage_ == Stage::Hello)
                    session_ = response.at("session").get<std::string>();
                if (session_.empty() || response.value("session", "") != session_)
                    throw std::runtime_error("Stale runtime session");
                waiting_ = false;
                if (!response.value("ok", false)) {
                    const auto error = response.value("error", "Runtime rejected request");
                    if (transaction_ && stage_ == Stage::Load &&
                        error.starts_with("Play restart required:")) {
                        notice_ =
                            "Schema changed; restarted play with compatible host-owned values. ";
                        launch(checkpoint_);
                        return;
                    }
                    throw std::runtime_error(error);
                }
                snapshot_ = response.at("scene");
                effective_ = response.at("effective_scene");
                timing_ = response.at("timing");
                ++snapshot_version_;
                if (stage_ == Stage::Hello) {
                    stage_ = Stage::Replace;
                    send({{"command", "replace"}, {"scene", initial_}});
                } else if (stage_ == Stage::Replace) {
                    if (!loading_.empty()) {
                        stage_ = Stage::Load;
                        send({{"command", "load_module"}, {"path", loading_}});
                    } else
                        begin_running();
                } else if (stage_ == Stage::Boundary) {
                    checkpoint_ = snapshot_;
                    previous_ = module_;
                    transaction_ = true;
                    loading_ = requested_;
                    requested_.clear();
                    stage_ = Stage::Load;
                    send({{"command", "load_module"}, {"path", loading_}});
                } else if (stage_ == Stage::Load) {
                    activation_generation_ = response.at("activation").at("generation");
                    if (probe_) {
                        stage_ = Stage::ProbeTick;
                        send({{"command", "step"}});
                    } else
                        begin_running();
                } else if (stage_ == Stage::ProbeTick) {
                    transaction_ = false;
                    module_ = loading_;
                    stage_ = Stage::Running;
                    status_ = "Probe passed one fixed tick.";
                }
                if (stage_ == Stage::Running && transaction_ &&
                    response.at("activation").value("state", "") == "active" &&
                    response.at("activation").value("generation", std::uint64_t{}) ==
                        activation_generation_) {
                    transaction_ = false;
                    module_ = loading_;
                    reload_result_ = Reload::Succeeded;
                    notice_ = "Reload committed after first live fixed tick. " + notice_;
                }
                if (stage_ == Stage::Running && !probe_)
                    update_status();
            }
            int exit_code = 0;
            if (SDL_WaitProcess(process_, false, &exit_code))
                throw std::runtime_error("Runtime exited (code " + std::to_string(exit_code) + ")");
            if (waiting_ && SDL_GetTicks() - sent_at_ > 5000)
                throw std::runtime_error("Runtime timed out");
            if (!waiting_ && stage_ == Stage::Running) {
                if (!requested_.empty()) {
                    prior_paused_ = paused();
                    desired_paused_ = prior_paused_;
                    stage_ = Stage::Boundary;
                    control_.clear();
                    send({{"command", "pause"}});
                } else if (!control_.empty()) {
                    const auto command = control_;
                    control_.clear();
                    send({{"command", command}});
                } else if (!probe_ && SDL_GetTicks() - sent_at_ >= 8)
                    send({{"command", "snapshot"}});
            }
        } catch (const std::exception& error) {
            const std::string diagnostic = error.what();
            if (transaction_ && !probe_ && !restoring_) {
                transaction_ = false;
                reload_result_ = Reload::Failed;
                loading_ = previous_;
                module_ = previous_;
                desired_paused_ = prior_paused_;
                restoring_ = true;
                notice_ =
                    "Reload failed; restored previous module and checkpoint: " + diagnostic + ". ";
                launch(checkpoint_);
            } else {
                const bool recover = !probe_ && !restoring_ && stage_ == Stage::Running;
                close_process();
                recoverable_ = recover;
                status_ = diagnostic + ". Authored scene is safe; " +
                          (recover ? "Recover resumes the last completed checkpoint."
                                   : "press Play to restart.");
            }
        }
    }

  private:
    enum class Stage { Hello, Replace, Load, Boundary, ProbeTick, Running };
    void close_process() {
        if (process_) {
            SDL_KillProcess(process_, true);
            SDL_WaitProcess(process_, true, nullptr);
            SDL_DestroyProcess(process_);
            process_ = nullptr;
        }
        outgoing_.clear();
        incoming_.clear();
        control_.clear();
        session_.clear();
        waiting_ = false;
    }
    void begin_running() {
        stage_ = Stage::Running;
        if (!desired_paused_)
            control_ = "resume";
        restoring_ = false;
    }
    void update_status() {
        status_ = transaction_ ? "Reload pending first tick. Step or Resume to activate. "
                  : paused()   ? "Paused. "
                               : "Playing in isolated runtime. ";
        status_ += notice_;
    }
    void launch(const Json& scene) {
        const auto initial = scene;
        close_process();
        initial_ = snapshot_ = effective_ = initial;
        timing_ = {{"paused", true}, {"tick", 0}};
        ++snapshot_version_;
        stage_ = Stage::Hello;
        request_id_ = 0;
        const char* args[] = {executable_.c_str(), nullptr};
        log_.clear();
        const auto properties = SDL_CreateProperties();
        const bool configured =
            properties &&
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                                  SDL_PROCESS_STDIO_APP) &&
            SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,
                                  SDL_PROCESS_STDIO_APP);
        if (configured)
            process_ = SDL_CreateProcessWithProperties(properties);
        if (properties)
            SDL_DestroyProperties(properties);
        if (!process_) {
            status_ = std::string("Cannot start play: ") + SDL_GetError();
            return;
        }
        status_ = "Starting play...";
        send({{"command", "hello"}});
    }
    void send(Json request) {
        sent_command_ = request.at("command").get<std::string>();
        request["protocol"] = 2;
        request["id"] = ++request_id_;
        if (!session_.empty())
            request["session"] = session_;
        outgoing_ = request.dump() + "\n";
        if (outgoing_.size() > 16 * 1024 * 1024)
            throw std::runtime_error("Play scene exceeds 16 MiB transport limit");
        waiting_ = true;
        sent_at_ = SDL_GetTicks();
    }
    SDL_Process* process_ = nullptr;
    Stage stage_ = Stage::Hello;
    Reload reload_result_ = Reload::Idle;
    std::string executable_, module_, loading_, requested_, previous_, notice_, session_, control_;
    Json checkpoint_, initial_, snapshot_, effective_, timing_ = {{"paused", true}, {"tick", 0}};
    bool transaction_ = false, restoring_ = false, probe_ = false, recoverable_ = false;
    bool prior_paused_ = false, desired_paused_ = false, waiting_ = false;
    std::uint64_t snapshot_version_ = 0, request_id_ = 0, activation_generation_ = 0;
    std::string sent_command_, outgoing_, incoming_, log_,
        status_ = "Stopped. Play uses a copy of your authored scene.";
    Uint64 sent_at_ = 0;
};
} // namespace forge
