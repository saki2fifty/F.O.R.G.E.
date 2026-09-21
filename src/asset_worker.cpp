#include "asset_worker.hpp"
#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif
namespace forge::asset_detail {
namespace {
void validate_limits(const WorkerLimits& limits) {
    if (limits.memory_bytes < 16 * 1024 * 1024 ||
        limits.memory_bytes > 16ull * 1024 * 1024 * 1024 || !limits.file_bytes ||
        limits.file_bytes > limits.total_bytes || limits.total_bytes > 8ull * 1024 * 1024 * 1024 ||
        !limits.files || limits.files > 65536 || !limits.seconds || limits.seconds > 3600 ||
        !limits.cpu_seconds || limits.cpu_seconds > 3600 || limits.cancellation_grace_ms > 5000)
        throw std::runtime_error("Invalid asset worker resource limits");
}
bool output_exceeded(const std::filesystem::path& path, const WorkerLimits& limits, bool flat) {
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
        !std::filesystem::is_directory(path))
        return true;
    std::uint64_t total = 0;
    unsigned count = 0;
    for (const auto& item : std::filesystem::directory_iterator(path)) {
        if (++count > limits.files)
            return true;
        const auto status = item.symlink_status();
        if (std::filesystem::is_symlink(status))
            return true;
        if (std::filesystem::is_regular_file(status)) {
            auto n = item.file_size();
            if (n > limits.file_bytes || n > limits.total_bytes - total)
                return true;
            total += n;
        } else if (flat || !std::filesystem::is_directory(status))
            return true;
    }
    return false;
}
} // namespace
void run_worker(WorkerKind kind, const std::filesystem::path& executable,
                const std::filesystem::path& staging, std::stop_token cancel,
                WorkerLimits resource) {
    if (!executable.is_absolute() || !std::filesystem::is_regular_file(executable))
        throw std::runtime_error("Packaged asset worker is missing");
    if (cancel.stop_requested())
        throw std::runtime_error("Asset build cancelled");
    validate_limits(resource);
    if (kind != WorkerKind::Animation && kind != WorkerKind::Navigation &&
        kind != WorkerKind::Script && kind != WorkerKind::Import && kind != WorkerKind::Schema)
        throw std::runtime_error("Unknown asset worker command");
    if (!staging.is_absolute() ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(staging)) ||
        !std::filesystem::is_directory(staging))
        throw std::runtime_error("Asset worker staging must be an existing absolute directory");
    const auto output = kind == WorkerKind::Import ? staging / "output" : staging;
    if (output_exceeded(output, resource, kind == WorkerKind::Import))
        throw std::runtime_error("Asset worker staging already violates output limits");
    const auto cancel_marker = staging / "cancel.request";
    if (kind == WorkerKind::Import &&
        std::filesystem::exists(std::filesystem::symlink_status(cancel_marker)))
        throw std::runtime_error("Asset worker staging has a stale cancellation marker");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(resource.seconds);
    std::optional<std::chrono::steady_clock::time_point> cancel_deadline;
    auto check = [&] {
        const auto now = std::chrono::steady_clock::now();
        if (now > deadline || output_exceeded(output, resource, kind == WorkerKind::Import))
            throw std::runtime_error("Asset build exceeded time or output limits");
        if (!cancel.stop_requested())
            return;
        if (kind != WorkerKind::Import || resource.cancellation_grace_ms == 0)
            throw std::runtime_error("Asset build cancelled");
        if (!cancel_deadline) {
            // Atomic marker-directory creation cannot truncate a worker-created
            // symlink target. Workers only test marker presence at safe boundaries.
            if (!std::filesystem::create_directory(cancel_marker))
                throw std::runtime_error("Cannot request cooperative asset cancellation");
            cancel_deadline = now + std::chrono::milliseconds(resource.cancellation_grace_ms);
        }
        if (now >= *cancel_deadline)
            throw std::runtime_error("Asset build cancellation grace expired");
    };
#ifdef _WIN32
    struct Handle {
        HANDLE value = nullptr;
        ~Handle() {
            if (value && value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    };
    Handle job{CreateJobObjectW(nullptr, nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(resource.memory_bytes);
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart =
        static_cast<LONGLONG>(resource.cpu_seconds) * 10000000;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                               &limits, sizeof(limits)))
        throw std::runtime_error("Cannot enforce converter process limits");
    // All variable paths use the explicit executable/cwd parameters; fixed arguments only.
    std::wstring command = kind == WorkerKind::Animation
                               ? L"gltf2ozz --file=source.gltf --config_file=config.json"
                           : kind == WorkerKind::Import ? L"forge_asset_build --build-asset"
                           : kind == WorkerKind::Script ? L"forge_tools --script-worker"
                           : kind == WorkerKind::Schema ? L"forge_runtime --inspect-sdk-worker"
                                                        : L"forge_nav_build --build-navigation";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, staging.c_str(), &startup,
                        &process))
        throw std::runtime_error("Cannot launch packaged asset worker");
    Handle child{process.hProcess}, thread{process.hThread};
    if (!AssignProcessToJobObject(job.value, child.value) ||
        ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(child.value, 1);
        WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("Cannot constrain asset worker");
    }
    try {
        while (WaitForSingleObject(child.value, 10) == WAIT_TIMEOUT) {
            check();
        }
        DWORD code = 1;
        if (!GetExitCodeProcess(child.value, &code) || code != 0)
            throw std::runtime_error("Asset build failed; previous assets retained");
    } catch (...) {
        TerminateJobObject(job.value, 1);
        WaitForSingleObject(child.value, 5000);
        throw;
    }
#else
    // Prepare C++ strings before fork; the child performs syscall setup and exec
    // without C++ allocation or locking.
    const auto file = executable.string(), cwd = staging.string();
    const auto parent = getpid();
    const auto child = fork();
    if (child < 0)
        throw std::runtime_error("Cannot start asset worker");
    if (child == 0) {
#ifdef __linux__
        // The synchronous supervisor thread remains alive until this child is
        // joined. Check the parent again to close the fork/prctl death race.
        if (prctl(PR_SET_PDEATHSIG, static_cast<long>(SIGKILL), 0L, 0L, 0L) || getppid() != parent)
            _exit(125);
#endif
        const rlimit memory{resource.memory_bytes, resource.memory_bytes},
            file_size{resource.file_bytes, resource.file_bytes},
            cpu{resource.cpu_seconds, resource.cpu_seconds}, core{0, 0};
        if (setpgid(0, 0) || setrlimit(RLIMIT_AS, &memory) || setrlimit(RLIMIT_FSIZE, &file_size) ||
            setrlimit(RLIMIT_CPU, &cpu) || setrlimit(RLIMIT_CORE, &core) || chdir(cwd.c_str()))
            _exit(125);
        int null = open("/dev/null", O_RDWR);
        if (null < 0)
            _exit(125);
        for (int fd = 0; fd < 3; ++fd)
            if (dup2(null, fd) < 0)
                _exit(125);
        if (null > 2)
            close(null);
        if (kind == WorkerKind::Import)
            execl(file.c_str(), "forge_asset_build", "--build-asset", static_cast<char*>(nullptr));
        else if (kind == WorkerKind::Script)
            execl(file.c_str(), "forge_tools", "--script-worker", static_cast<char*>(nullptr));
        else if (kind == WorkerKind::Schema)
            execl(file.c_str(), "forge_runtime", "--inspect-sdk-worker",
                  static_cast<char*>(nullptr));
        else if (kind == WorkerKind::Navigation)
            execl(file.c_str(), "forge_nav_build", "--build-navigation",
                  static_cast<char*>(nullptr));
        else
            execl(file.c_str(), "gltf2ozz", "--file=source.gltf", "--config_file=config.json",
                  static_cast<char*>(nullptr));
        _exit(126);
    }
    int status = 0;
    bool child_owned = true;
    try {
        for (;;) {
            siginfo_t info{};
            const auto result =
                waitid(P_PID, static_cast<id_t>(child), &info, WEXITED | WNOHANG | WNOWAIT);
            if (result == 0 && info.si_pid == child) {
                // Keep the zombie leader's PID reserved until the process group
                // is terminated; reaping first would allow a PID-reuse race.
                kill(-child, SIGKILL);
                while (waitpid(child, &status, 0) < 0) {
                    if (errno != EINTR) {
                        if (errno == ECHILD)
                            child_owned = false;
                        throw std::runtime_error("Cannot reap asset worker");
                    }
                }
                break;
            }
            if (result < 0 && errno != EINTR) {
                if (errno == ECHILD)
                    child_owned = false;
                throw std::runtime_error("Cannot wait for asset worker");
            }
            check();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (...) {
        if (child_owned) {
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
        }
        throw;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("Asset build failed; previous assets retained");
#endif
    if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("Asset build exceeded its wall-time limit");
    if (cancel.stop_requested() || output_exceeded(output, resource, kind == WorkerKind::Import))
        throw std::runtime_error("Asset build cancelled or exceeded output limits");
}
} // namespace forge::asset_detail
