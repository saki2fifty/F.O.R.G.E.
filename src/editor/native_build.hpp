#pragma once
#include "play.hpp"
#include <fstream>
#include <map>
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
    void start(const std::vector<std::string>& arguments) {
        close();
        std::vector<const char*> args;
        for (const auto& argument : arguments)
            args.push_back(argument.c_str());
        args.push_back(nullptr);
        const auto properties = SDL_CreateProperties();
        if (!properties)
            throw std::runtime_error(SDL_GetError());
        const bool configured =
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

class NativeBuild {
  public:
    NativeBuild(std::filesystem::path project, std::filesystem::path sdk, std::string runtime)
        : source_(std::move(project) / "Native"),
          work_(source_.parent_path() / ".forge" / "native"), sdk_(std::move(sdk)),
          runtime_(std::move(runtime)) {}
    bool busy() const { return phase_ != Phase::Idle; }
    bool has_source() const { return std::filesystem::exists(source_ / "CMakeLists.txt"); }
    const std::string& artifact() const { return active_; }
    const std::string& status() const { return status_; }
    const std::string& log() const { return log_; }
    std::string source_path() const { return (source_ / "gameplay.cpp").string(); }
    bool auto_build = false;
    std::string cmake = "cmake", ninja = "ninja";

    void create_source() {
        if (std::filesystem::exists(source_))
            throw std::runtime_error(
                "Native directory already exists; no sources were overwritten");
        std::filesystem::create_directories(source_.parent_path());
        auto staging = source_;
        staging += ".pending";
        if (!std::filesystem::create_directory(staging))
            throw std::runtime_error(
                "Native staging directory already exists; inspect it before retrying");
        try {
            std::filesystem::copy_file(sdk_ / "samples/native/movement.c",
                                       staging / "gameplay.cpp");
            std::filesystem::copy_file(sdk_ / "samples/native/CMakeLists.txt",
                                       staging / "CMakeLists.txt");
            std::filesystem::rename(staging, source_);
            status_ = "Gameplay source created. Build it, then press Play.";
        } catch (...) {
            std::filesystem::remove_all(staging);
            throw;
        }
    }
    void build() {
        if (busy())
            return;
        try {
            if (!has_source())
                throw std::runtime_error("Create gameplay source first");
            std::filesystem::create_directories(work_);
            log_.clear();
            log_file_.close();
            log_file_.open(work_ / "build.log", std::ios::trunc);
            if (!log_file_)
                throw std::runtime_error("Cannot open native build log");
            observed_ = fingerprint();
            command_.start({cmake, "-S", source_.string(), "-B", (work_ / "build").string(), "-G",
                            "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DFORGE_SDK=" + sdk_.string(),
                            "-DCMAKE_MAKE_PROGRAM=" + ninja,
                            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"});
            phase_ = Phase::Configure;
            status_ = "Configuring native build; play continues.";
        } catch (const std::exception& error) {
            fail(error.what());
        }
    }
    void pump(PlaySession& play, const Json& authored) {
        try {
            watch();
            if (phase_ == Phase::Configure || phase_ == Phase::Compile) {
                std::string output;
                int exit_code = 0;
                const bool finished = command_.pump(output, exit_code);
                append(output);
                if (!finished)
                    return;
                if (exit_code != 0)
                    throw std::runtime_error("Compiler command failed (code " +
                                             std::to_string(exit_code) +
                                             "). Check the build output.");
                if (phase_ == Phase::Configure) {
                    command_.start({cmake, "--build", (work_ / "build").string(), "--target",
                                    "gameplay", "--parallel", "2"});
                    phase_ = Phase::Compile;
                    status_ = "Compiling gameplay; previous module remains active.";
                } else {
#ifdef _WIN32
                    const char* name = "gameplay.dll";
#else
                    const char* name = "gameplay.so";
#endif
                    const auto modules = work_ / "modules";
                    std::filesystem::create_directories(modules);
                    std::filesystem::path version;
                    do {
                        version = modules / std::to_string(SDL_GetPerformanceCounter());
                    } while (!std::filesystem::create_directory(version));
                    candidate_ = std::filesystem::absolute(version / name).string();
                    std::filesystem::copy_file(work_ / "build" / name, candidate_);
                    probe_.start(runtime_, play.active() ? play.snapshot() : authored, candidate_,
                                 true);
                    phase_ = Phase::Probe;
                    status_ = "Validating replacement in a disposable runtime.";
                }
            } else if (phase_ == Phase::Probe) {
                probe_.pump();
                if (!probe_.active())
                    throw std::runtime_error("Candidate rejected: " + probe_.status() + "\n" +
                                             probe_.log());
                if (probe_.ready()) {
                    probe_.stop();
                    if (play.active()) {
                        phase_ = Phase::AwaitPlay;
                    } else {
                        commit();
                    }
                }
            } else if (phase_ == Phase::AwaitPlay) {
                if (!play.active())
                    commit();
                else if (play.ready()) {
                    play.reload(candidate_);
                    phase_ = Phase::Reload;
                    status_ = "Switching gameplay at a runtime command boundary.";
                }
            } else if (phase_ == Phase::Reload) {
                if (play.reload_result() == PlaySession::Reload::Succeeded)
                    commit();
                else if (!play.active() || play.reload_result() == PlaySession::Reload::Failed)
                    throw std::runtime_error("Reload failed; previous artifact retained. " +
                                             play.status());
            }
        } catch (const std::exception& error) {
            fail(error.what());
        }
    }

  private:
    enum class Phase { Idle, Configure, Compile, Probe, AwaitPlay, Reload };
    using Stamp =
        std::map<std::filesystem::path, std::pair<std::filesystem::file_time_type, std::uintmax_t>>;
    Stamp fingerprint() const {
        Stamp result;
        if (!std::filesystem::exists(source_))
            return result;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(source_)) {
            if (!entry.is_regular_file())
                continue;
            const auto ext = entry.path().extension();
            if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".cmake" ||
                entry.path().filename() == "CMakeLists.txt")
                result.emplace(entry.path(),
                               std::make_pair(entry.last_write_time(), entry.file_size()));
        }
        return result;
    }
    void watch() {
        const auto now = SDL_GetTicks();
        if (!auto_build || busy() || now - scanned_ < 500)
            return;
        scanned_ = now;
        auto current = fingerprint();
        if (current != observed_) {
            observed_ = std::move(current);
            changed_ = now;
            dirty_ = true;
        } else if (dirty_ && now - changed_ >= 500) {
            dirty_ = false;
            build();
        }
    }
    void append(const std::string& text) {
        log_ += text;
        if (log_.size() > 131072)
            log_.erase(0, log_.size() - 131072);
        if (log_file_) {
            log_file_ << text;
            log_file_.flush();
        }
    }
    void fail(const std::string& error) {
        command_.close();
        probe_.stop();
        phase_ = Phase::Idle;
        status_ = error + " Previous usable artifact preserved.";
        append("\n" + status_ + "\n");
    }
    void commit() {
        active_ = candidate_;
        phase_ = Phase::Idle;
        status_ = "Build validated. Gameplay is ready for Play / Restart.";
        append("\n" + status_ + "\n");
    }
    std::filesystem::path source_, work_, sdk_;
    std::string runtime_, active_, candidate_, log_;
    std::string status_ = "Create a gameplay source, then Build & Reload.";
    std::ofstream log_file_;
    Phase phase_ = Phase::Idle;
    BuildCommand command_;
    PlaySession probe_;
    Stamp observed_;
    Uint64 scanned_ = 0, changed_ = 0;
    bool dirty_ = false;
};
} // namespace forge
