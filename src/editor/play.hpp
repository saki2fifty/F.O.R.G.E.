#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <forge/build.hpp>
#include <forge/input.hpp>
#include <forge/project_paths.hpp>
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
    const Json& input_status() const { return input_status_; }
    const Json& ui_snapshot() const { return ui_snapshot_; }
    const Json& ui_ack() const { return ui_ack_; }
    bool submit_ui(const Json& command) {
        if (!ready() || !ui_command_.is_null() || transaction_)
            return false;
        if (command.value("session", "") != session_ || ui_snapshot_.is_null() ||
            command.at("generation") != ui_snapshot_.at("generation"))
            return false;
        ui_command_ = command;
        return true;
    }
    void configure(double hz, InputMap map, Double3 gravity = {0, -9.81, 0},
                   std::filesystem::path project = {}, bool exact_sdk = false) {
        if (active())
            throw std::runtime_error("Stop Play before configuring input/settings");
        if (!std::isfinite(hz) || hz < 1 || hz > 240)
            throw std::runtime_error("Invalid simulation frequency");
        gravity_ = gravity;
        audio_project_ = path_utf8(project);
        if (exact_sdk && project.empty())
            throw std::runtime_error("Exact SDK Play requires a project root");
        exact_sdk_ = exact_sdk;
        simulation_hz_ = hz;
        input_map_ = map.source();
    }
    void input_event(InputEvent event) {
        if (!ready())
            return;
        if (input_events_.size() >= 4096) {
            input_events_.clear();
            input_events_.push_back({{}, 0, true});
            notice_ = "Input queue overflow: controls released. ";
            return;
        }
        input_events_.push_back(std::move(event));
    }
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
        launch(snapshot_, recovery_);
    }
    // Caller has executed this immutable artifact in a separate fixed-tick probe.
    void reload(const std::string& path) {
        if (exact_sdk_)
            throw std::runtime_error("Exact SDK registrations require Stop, rebuild, then Play");
        if (!ready() || recovery_.is_null())
            throw std::runtime_error(
                "Play is not ready for reload. Wait for asset loading or resolve Console errors.");
        if (transaction_) {
            // No candidate tick completed: discard its world before superseding.
            // Keep the original known-good artifact/checkpoint and prior run policy.
            loading_ = path;
            notice_ = "Previous pending activation cancelled. ";
            launch(checkpoint_, checkpoint_recovery_);
        } else {
            requested_ = path;
        }
        reload_result_ = Reload::Pending;
    }
    Double3 gravity() const { return gravity_; }
    const Json& recovery() const { return recovery_; }
    const Json& snapshot() const { return snapshot_; }
    const Json& effective_snapshot() const { return effective_; }
    std::uint64_t snapshot_version() const { return snapshot_version_; }
    const std::string& status() const { return status_; }
    const std::string& log() const { return log_; }
    const Json& diagnostics() const { return diagnostics_; }
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
               bool probe = false, const Json& recovery = Json()) {
        if (exact_sdk_ && (!module.empty() || !recovery.is_null()))
            throw std::runtime_error("Exact SDK Play starts from authored state; ABI1 reload and "
                                     "partial custom-state recovery are unavailable");
        const auto initial = scene;
        stop();
        executable_ = executable;
        loading_ = module;
        module_.clear();
        previous_.clear();
        checkpoint_ = initial;
        checkpoint_recovery_ = recovery;
        probe_ = probe;
        desired_paused_ = probe;
        prior_paused_ = probe;
        transaction_ = !module.empty();
        restoring_ = false;
        reload_result_ = Reload::Idle;
        notice_.clear();
        launch(initial, recovery);
    }
    void pump() {
        if (!process_)
            return;
        try {
            for (unsigned chunk = 0; !outgoing_.empty() && chunk < 32; ++chunk) {
                auto* input = SDL_GetProcessInput(process_);
                const auto count = SDL_WriteIO(input, outgoing_.data(),
                                               std::min<std::size_t>(1024, outgoing_.size()));
                outgoing_.erase(0, count);
                if (!count && SDL_GetIOStatus(input) != SDL_IO_STATUS_NOT_READY)
                    throw std::runtime_error("Runtime input closed");
                if (!count)
                    break;
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
                        launch(checkpoint_, checkpoint_recovery_);
                        return;
                    }
                    throw std::runtime_error(error);
                }
                if (stage_ == Stage::Hello && exact_sdk_) {
                    const auto info = response.value("runtime_contract", Json::object());
                    if (info.value("profile", "") != "shared-native-sdk" ||
                        !info.value("sdk_project", false) ||
                        info.value("source_commit", "") != forge::source_commit)
                        throw std::runtime_error("SDK runtime does not match this editor source "
                                                 "or shared SDK profile. Select the matching "
                                                 "Native SDK installation in Gameplay Code.");
                }
                const auto diagnostics = response.value("diagnostics", Json::array());
                if (diagnostics != diagnostics_) {
                    for (const auto& d : diagnostics)
                        if (std::find(diagnostics_.begin(), diagnostics_.end(), d) ==
                            diagnostics_.end())
                            log_ +=
                                d.value("category", "runtime") + ": " + d.value("text", "") + "\n";
                    if (log_.size() > 131072)
                        log_.erase(0, log_.size() - 131072);
                    diagnostics_ = diagnostics;
                }
                snapshot_ = response.at("scene");
                recovery_ = response.at("recovery");
                effective_ = response.at("effective_scene");
                timing_ = response.at("timing");
                input_status_ = response.value("input", Json::object());
                auto next_ui = response.value("ui", Json());
                if (!next_ui.is_null() && !ui_snapshot_.is_null() &&
                    next_ui.at("generation") != ui_snapshot_.at("generation"))
                    ui_command_ = ui_ack_ =
                        nullptr; // Old queued requests cannot cross replacement.
                ui_snapshot_ = std::move(next_ui);
                if (response.contains("ui_ack"))
                    ui_ack_ = response.at("ui_ack");
                ++snapshot_version_;
                if (stage_ == Stage::Hello) {
                    stage_ = Stage::Replace;
                    Json replacement = {{"command", "replace"}, {"scene", initial_}};
                    if (!initial_recovery_.is_null()) {
                        replacement["recovery"] = initial_recovery_;
                        replacement["recovery_session"] = initial_recovery_.at("session");
                        replacement["recovery_tick"] = initial_recovery_.at("tick");
                    }
                    send(std::move(replacement));
                } else if (stage_ == Stage::Replace) {
                    if (!loading_.empty()) {
                        stage_ = Stage::Load;
                        send({{"command", "load_module"}, {"path", loading_}});
                    } else
                        begin_running();
                } else if (stage_ == Stage::Boundary) {
                    if (recovery_.is_null()) {
                        requested_.clear();
                        reload_result_ = Reload::Failed;
                        notice_ = "Reload postponed: Play has no complete recovery snapshot. "
                                  "Previous module retained. ";
                        stage_ = Stage::Running;
                        if (!prior_paused_)
                            control_ = "resume";
                        return;
                    }
                    checkpoint_ = snapshot_;
                    checkpoint_recovery_ = recovery_;
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
                throw std::runtime_error(
                    "Runtime timed out during " + sent_command_ + " (request " +
                    std::to_string(request_id_) + ", outgoing " + std::to_string(outgoing_.size()) +
                    ", incoming " + std::to_string(incoming_.size()) + " bytes)");
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
                } else if (!ui_command_.is_null()) {
                    auto command = std::move(ui_command_);
                    ui_command_ = nullptr;
                    send({{"command", "ui"}, {"ui_command", std::move(command)}});
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
                launch(checkpoint_, checkpoint_recovery_);
            } else {
                const bool recover = !exact_sdk_ && !probe_ && !restoring_ &&
                                     stage_ == Stage::Running && !recovery_.is_null();
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
        ui_snapshot_ = ui_ack_ = ui_command_ = nullptr;
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
    void launch(const Json& scene, const Json& recovery = Json()) {
        const auto recovery_copy = recovery;
        const auto initial = scene;
        close_process();
        initial_ = snapshot_ = effective_ = initial;
        initial_recovery_ = recovery_ = recovery_copy;
        timing_ = {{"paused", true}, {"tick", 0}};
        ++snapshot_version_;
        stage_ = Stage::Hello;
        request_id_ = 0;
        input_events_.clear();
        input_status_ = Json::object();
        std::vector<const char*> args{executable_.c_str()};
        if (!audio_project_.empty()) {
            args.push_back(exact_sdk_ ? "--sdk-project" : "--project");
            args.push_back(audio_project_.c_str());
            args.push_back("--audio");
            args.push_back(probe_ ? "offline" : "device");
            if (!probe_) {
                args.push_back("--ui");
                args.push_back("on");
            }
        }
        args.push_back(nullptr);
        log_.clear();
        diagnostics_ = Json::array();
        const auto properties = SDL_CreateProperties();
        const bool configured =
            properties &&
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data()) &&
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
        send({{"command", "hello"},
              {"simulation_hz", simulation_hz_},
              {"input_map", input_map_},
              {"gravity", gravity_}});
    }
    void send(Json request) {
        sent_command_ = request.at("command").get<std::string>();
        if ((sent_command_ == "snapshot" || sent_command_ == "step" || sent_command_ == "resume" ||
             sent_command_ == "pause") &&
            !input_events_.empty()) {
            request["input_events"] = input_events_;
            input_events_.clear();
        }
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
    Double3 gravity_{0, -9.81, 0};
    Json recovery_, initial_recovery_, checkpoint_recovery_;
    double simulation_hz_ = 60;
    Json input_map_ = InputMap{}.source(), input_status_ = Json::object();
    Json ui_snapshot_, ui_ack_, ui_command_;
    std::vector<InputEvent> input_events_;
    SDL_Process* process_ = nullptr;
    Stage stage_ = Stage::Hello;
    Reload reload_result_ = Reload::Idle;
    std::string executable_, module_, loading_, requested_, previous_, notice_, session_, control_;
    Json checkpoint_, initial_, snapshot_, effective_, timing_ = {{"paused", true}, {"tick", 0}};
    Json diagnostics_ = Json::array();
    std::string audio_project_;
    bool transaction_ = false, restoring_ = false, probe_ = false, recoverable_ = false;
    bool exact_sdk_ = false;
    bool prior_paused_ = false, desired_paused_ = false, waiting_ = false;
    std::uint64_t snapshot_version_ = 0, request_id_ = 0, activation_generation_ = 0;
    std::string sent_command_, outgoing_, incoming_, log_,
        status_ = "Stopped. Play uses a copy of your authored scene.";
    Uint64 sent_at_ = 0;
};
} // namespace forge
