#include "command_worker.hpp"
#include "bounded_json.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#if defined(_WIN32) || defined(__linux__)
bool read_input(void* bytes, std::size_t size) {
    auto* next = static_cast<char*>(bytes);
    while (size) {
#ifdef _WIN32
        DWORD count = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), next, DWORD(size), &count, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE)
                return false;
            throw std::runtime_error("Cannot read command launch input");
        }
#else
        const auto count = read(STDIN_FILENO, next, size);
        if (count < 0 && errno == EINTR)
            continue;
        require(count >= 0, "Cannot read command launch input");
#endif
        if (!count)
            return false;
        next += count;
        size -= std::size_t(count);
    }
    return true;
}
void receipt(const Json& value) {
    const auto text = value.dump() + "\n";
    const auto* next = text.data();
    auto remaining = text.size();
    while (remaining) {
#ifdef _WIN32
        DWORD count = 0;
        if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), next, DWORD(remaining), &count, nullptr))
            return; // Parent closed its pipe; there is no caller left to notify.
#else
        const auto count = write(STDERR_FILENO, next, remaining);
        if (count < 0 && errno == EINTR)
            continue;
#endif
        if (count <= 0)
            return;
        next += count;
        remaining -= std::size_t(count);
    }
}
std::vector<std::string> arguments() {
    std::array<unsigned char, 4> header{};
    require(read_input(header.data(), header.size()), "Command cancelled before launch");
    const auto size = (std::uint32_t(header[0]) << 24) | (std::uint32_t(header[1]) << 16) |
                      (std::uint32_t(header[2]) << 8) | header[3];
    require(size > 0 && size <= 1024 * 1024, "Invalid command launch record size");
    std::string text(size, '\0');
    require(read_input(text.data(), text.size()), "Command cancelled before launch");
    const auto request =
        asset_detail::parse_bounded_json(std::as_bytes(std::span(text)), 1024 * 1024, 4096, 4);
    require(request.is_object() && request.size() == 2 &&
                request.at("version").is_number_integer() && request.at("version") == 1 &&
                request.at("args").is_array(),
            "Invalid command launch record");
    const auto result = request.at("args").get<std::vector<std::string>>();
    require(!result.empty() && result.size() <= 1024 && !result.front().empty(),
            "Invalid command arguments");
    for (const auto& argument : result)
        require(argument.find('\0') == std::string::npos, "Command argument contains NUL");
    return result;
}
bool cancellation_pending(int timeout_ms = 0) {
#ifdef _WIN32
    if (timeout_ms)
        Sleep(DWORD(timeout_ms));
    DWORD available = 0;
    if (!PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE), nullptr, 0, nullptr, &available, nullptr)) {
        require(GetLastError() == ERROR_BROKEN_PIPE, "Cannot read command control pipe");
        return true;
    }
    return available > 0;
#else
    pollfd input{STDIN_FILENO, POLLIN | POLLHUP, 0};
    int ready;
    do {
        ready = poll(&input, 1, timeout_ms);
    } while (ready < 0 && errno == EINTR);
    require(ready >= 0 && !(input.revents & POLLNVAL), "Cannot read command control pipe");
    return ready && input.revents;
#endif
}
#endif

#ifdef _WIN32
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void close() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
        value = nullptr;
    }
    ~Handle() { close(); }
};
std::wstring wide(std::string_view text) {
    if (text.empty())
        return {};
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                          int(text.size()), nullptr, 0);
    require(size > 0, "Command argument is not valid UTF-8");
    std::wstring result(size, L'\0');
    require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
                                result.data(), size) == size,
            "Cannot convert command argument");
    return result;
}
std::wstring command_line(const std::vector<std::string>& args) {
    std::wstring result;
    for (const auto& arg : args) {
        if (!result.empty())
            result += L' ';
        const auto value = wide(arg);
        if (!value.empty() && value.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
            result += value;
            continue;
        }
        result += L'"';
        unsigned slashes = 0;
        for (const auto c : value) {
            if (c == L'\\') {
                ++slashes;
                continue;
            }
            result.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
            result += c;
            slashes = 0;
        }
        result.append(2 * slashes, L'\\');
        result += L'"';
    }
    require(result.size() < 32767, "Command exceeds Windows command-line limit");
    return result;
}
struct AttributeList {
    std::vector<unsigned char> bytes;
    LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;
    explicit AttributeList(std::array<HANDLE, 2>& handles) {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        require(size > 0, "Cannot size command handle list");
        bytes.resize(size);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(bytes.data());
        require(InitializeProcThreadAttributeList(list, 1, 0, &size),
                "Cannot create command handle list");
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
                                       sizeof(handles), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(list);
            throw std::runtime_error("Cannot isolate command handles");
        }
        value = list;
    }
    ~AttributeList() { DeleteProcThreadAttributeList(value); }
};
class Supervisor {
    Handle job_, child_, thread_;
    bool attached_ = false;

  public:
    bool cancelled = false;
    void launch(const std::vector<std::string>& args) {
        auto line = command_line(args);
        job_.value = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        require(job_.value && SetInformationJobObject(job_.value, JobObjectExtendedLimitInformation,
                                                      &limits, sizeof(limits)),
                "Cannot create command process Job");
        // Only this isolated helper briefly owns inheritable target handles.
        // Neither editor handles nor the private control/receipt pipes transfer.
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        Handle output;
        require(input.value != INVALID_HANDLE_VALUE &&
                    DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE),
                                    GetCurrentProcess(), &output.value, 0, TRUE,
                                    DUPLICATE_SAME_ACCESS),
                "Cannot prepare command streams");
        std::array<HANDLE, 2> handles{input.value, output.value};
        AttributeList attributes(handles);
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input.value;
        startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = output.value;
        startup.lpAttributeList = attributes.value;
        PROCESS_INFORMATION process{};
        require(CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                               CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                               nullptr, nullptr, &startup.StartupInfo, &process),
                "Cannot launch command executable");
        child_.value = process.hProcess;
        thread_.value = process.hThread;
        require(AssignProcessToJobObject(job_.value, child_.value),
                "Cannot contain command in process Job");
        attached_ = true;
        require(ResumeThread(thread_.value) != DWORD(-1), "Cannot resume command executable");
        thread_.close();
    }
    int wait() {
        for (;;) {
            const auto state = WaitForSingleObject(child_.value, 20);
            if (state == WAIT_OBJECT_0)
                break;
            require(state == WAIT_TIMEOUT, "Cannot wait for command executable");
            cancelled = cancellation_pending();
            if (cancelled)
                return 130;
        }
        DWORD code = 0;
        require(GetExitCodeProcess(child_.value, &code), "Cannot read command exit status");
        return int(code);
    }
    void retire() {
        if (!child_.value && !attached_)
            return;
        if (!attached_) {
            // Failed Job assignment leaves a suspended child that ran no code.
            require(TerminateProcess(child_.value, 1), "Cannot retire suspended command");
            require(WaitForSingleObject(child_.value, INFINITE) == WAIT_OBJECT_0,
                    "Cannot join suspended command");
        } else {
            require(TerminateJobObject(job_.value, 1), "Cannot terminate command process Job");
        }
        thread_.close();
        child_.close();
        // ActiveProcesses may include terminated processes with retained OS
        // references. This is a completion condition, not a fixed-time promise.
        if (attached_)
            for (;;) {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                require(QueryInformationJobObject(job_.value, JobObjectBasicAccountingInformation,
                                                  &info, sizeof(info), nullptr),
                        "Cannot verify command process Job retirement");
                if (!info.ActiveProcesses)
                    break;
                Sleep(10);
            }
        attached_ = false;
        job_.close();
    }
};
#elif defined(__linux__)
class Supervisor {
    pid_t child_ = -1;
    bool launched_ = false;
    static void signal(pid_t process) {
        require(kill(process, SIGKILL) == 0 || errno == ESRCH,
                "Cannot terminate command descendant");
    }

  public:
    bool cancelled = false;
    void launch(const std::vector<std::string>& args) {
        require(prctl(PR_SET_CHILD_SUBREAPER, 1L, 0L, 0L, 0L) == 0,
                "Cannot establish isolated command descendant ownership");
        std::vector<char*> argv;
        for (const auto& value : args)
            argv.push_back(const_cast<char*>(value.c_str()));
        argv.push_back(nullptr);
        const auto parent = getpid();
        child_ = fork();
        require(child_ >= 0, "Cannot fork command executable");
        if (child_ == 0) {
            if (setpgid(0, 0) || prctl(PR_SET_PDEATHSIG, static_cast<long>(SIGKILL), 0L, 0L, 0L) ||
                getppid() != parent)
                _exit(125);
            const auto null = open("/dev/null", O_RDONLY);
            if (null < 0 || dup2(null, STDIN_FILENO) < 0 || dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
                _exit(125);
            if (null > STDERR_FILENO)
                close(null);
            ::signal(SIGPIPE, SIG_DFL);
            execvp(argv.front(), argv.data());
            _exit(127);
        }
        launched_ = true;
    }
    int wait() {
        for (;;) {
            siginfo_t info{};
            if (waitid(P_PID, id_t(child_), &info, WEXITED | WNOHANG | WNOWAIT) < 0) {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error("Cannot wait for command executable");
            }
            if (info.si_pid == child_) {
                // Preserve leader identity until retire has signalled its group.
                return info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status;
            }
            if (cancellation_pending(20)) {
                cancelled = true; // Any control byte or parent EOF requests retirement.
                return 130;
            }
        }
    }
    void retire() {
        if (!launched_)
            return;
        if (child_ > 0) {
            signal(-child_);
            signal(child_);
            while (waitpid(child_, nullptr, 0) < 0) {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error("Cannot reap command executable");
            }
            child_ = -1;
        }
        const auto children = "/proc/self/task/" + std::to_string(getpid()) + "/children";
        for (;;) {
            // Only this single-threaded helper reaps these children. Their PIDs
            // cannot be reused between enumeration and signalling, even on exit.
            std::ifstream input(children);
            require(bool(input), "Cannot enumerate owned command descendants");
            pid_t child = 0;
            while (input >> child) {
                require(child > 0, "Invalid owned command descendant");
                signal(child);
            }
            require(input.eof(), "Cannot inspect owned command descendants");
            for (;;) {
                const auto result = waitpid(-1, nullptr, WNOHANG);
                if (result > 0 || (result < 0 && errno == EINTR))
                    continue;
                if (result < 0 && errno == ECHILD) {
                    launched_ = false;
                    return;
                }
                require(result == 0, "Cannot reap command descendants");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};
#endif
} // namespace

int command_worker_stdio() noexcept {
#if defined(_WIN32) || defined(__linux__)
#ifndef _WIN32
    ::signal(SIGPIPE, SIG_IGN);
#endif
    Supervisor supervisor;
    int code = 125;
    std::string error;
    try {
        const auto args = arguments();
        supervisor.cancelled = cancellation_pending();
        if (supervisor.cancelled)
            code = 130;
        else {
            supervisor.launch(args);
            code = supervisor.wait();
        }
    } catch (const std::exception& failure) {
        error = std::string(failure.what()).substr(0, 2048);
    } catch (...) {
        error = "Command supervision failed";
    }
    bool retired = false;
    try {
        supervisor.retire();
        retired = true;
    } catch (const std::exception& failure) {
        error = std::string(failure.what()).substr(0, 2048);
    } catch (...) {
        error = "Command retirement could not be verified";
    }
    try {
        receipt({{"version", 1},
                 {"retired", retired},
                 {"exit_code", code},
                 {"cancelled", supervisor.cancelled},
                 {"error", error}});
    } catch (...) {
        return 2;
    }
    return retired ? 0 : 2;
#else
    return 2; // Unsupported OS cannot claim descendant retirement.
#endif
}
} // namespace forge
