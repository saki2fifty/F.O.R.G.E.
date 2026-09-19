#include "asset_worker.hpp"
#include <chrono>
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
#endif
namespace forge::asset_detail {
namespace {
constexpr std::uint64_t memory_limit = 512ull * 1024 * 1024, output_limit = 32ull * 1024 * 1024;
bool output_exceeded(const std::filesystem::path& path) {
    std::uint64_t total = 0;
    for (const auto& item : std::filesystem::directory_iterator(path))
        if (item.is_regular_file()) {
            auto n = item.file_size();
            if (n > 16 * 1024 * 1024 || n > output_limit - total)
                return true;
            total += n;
        }
    return false;
}
} // namespace
void run_worker(WorkerKind kind, const std::filesystem::path& executable,
                const std::filesystem::path& staging, std::stop_token cancel) {
    if (!executable.is_absolute() || !std::filesystem::is_regular_file(executable))
        throw std::runtime_error("Packaged asset worker is missing");
    if (cancel.stop_requested())
        throw std::runtime_error("Asset build cancelled");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
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
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                              JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.ProcessMemoryLimit = memory_limit;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                               &limits, sizeof(limits)))
        throw std::runtime_error("Cannot enforce converter process limits");
    // All variable paths use the explicit executable/cwd parameters; fixed arguments only.
    std::wstring command = kind == WorkerKind::Animation
                               ? L"gltf2ozz --file=source.gltf --config_file=config.json"
                           : kind == WorkerKind::Script ? L"forge_tools --script-worker"
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
            if (cancel.stop_requested() || std::chrono::steady_clock::now() > deadline ||
                output_exceeded(staging))
                throw std::runtime_error("Asset build cancelled or exceeded resource limits");
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
    // Prepare strings before fork; child uses only async-signal-safe POSIX operations.
    const auto file = executable.string(), cwd = staging.string();
    const auto child = fork();
    if (child < 0)
        throw std::runtime_error("Cannot start asset worker");
    if (child == 0) {
        const rlimit memory{memory_limit, memory_limit},
            file_size{16 * 1024 * 1024, 16 * 1024 * 1024}, cpu{25, 25}, core{0, 0};
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
        if (kind == WorkerKind::Script)
            execl(file.c_str(), "forge_tools", "--script-worker", static_cast<char*>(nullptr));
        else if (kind == WorkerKind::Navigation)
            execl(file.c_str(), "forge_nav_build", "--build-navigation",
                  static_cast<char*>(nullptr));
        else
            execl(file.c_str(), "gltf2ozz", "--file=source.gltf", "--config_file=config.json",
                  static_cast<char*>(nullptr));
        _exit(126);
    }
    int status = 0;
    try {
        for (;;) {
            auto result = waitpid(child, &status, WNOHANG);
            if (result == child)
                break;
            if (result < 0 && errno != EINTR)
                throw std::runtime_error("Cannot wait for asset worker");
            if (cancel.stop_requested() || std::chrono::steady_clock::now() > deadline ||
                output_exceeded(staging))
                throw std::runtime_error("Asset build cancelled or exceeded resource limits");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    } catch (...) {
        kill(-child, SIGKILL);
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        throw;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("Asset build failed; previous assets retained");
#endif
    if (cancel.stop_requested() || output_exceeded(staging))
        throw std::runtime_error("Asset build cancelled or exceeded output limits");
}
} // namespace forge::asset_detail
