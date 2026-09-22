#include "worker_stage_lease.hpp"
#include "native_io_path.hpp"
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace forge::asset_detail {
struct WorkerStageLease::State {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~State() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    int handle = -1;
    ~State() {
        // Never LOCK_UN: forked workers share the open file description.
        // Its final close, including the worker's copy, releases ownership.
        if (handle >= 0)
            close(handle);
    }
#endif
};
WorkerStageLease::WorkerStageLease() : state_(std::make_unique<State>()) {}
WorkerStageLease::~WorkerStageLease() = default;
bool WorkerStageLease::open(const std::filesystem::path& file, bool create) {
    const auto path = native_io_path(file);
    if (native_io_path(std::filesystem::weakly_canonical(path)) != path)
        throw std::runtime_error("Worker ownership marker must not redirect");
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    state_->handle =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                    create ? &attributes : nullptr, create ? CREATE_NEW : OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (state_->handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (!create && error == ERROR_SHARING_VIOLATION)
            return false;
        throw std::runtime_error("Cannot acquire worker ownership marker (Windows error " +
                                 std::to_string(error) + ")");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(state_->handle, &info) || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        throw std::runtime_error("Worker ownership marker must be an ordinary unaliased file");
#else
    state_->handle = ::open(
        path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT | O_EXCL : 0), 0600);
    if (state_->handle < 0)
        throw std::runtime_error("Cannot open worker ownership marker");
    struct stat held{}, current{};
    if (fstat(state_->handle, &held) || lstat(path.c_str(), &current) || !S_ISREG(held.st_mode) ||
        held.st_nlink != 1 || held.st_dev != current.st_dev || held.st_ino != current.st_ino)
        throw std::runtime_error("Worker ownership marker must be an ordinary unaliased file");
    if (flock(state_->handle, LOCK_EX | LOCK_NB)) {
        if (!create && (errno == EWOULDBLOCK || errno == EAGAIN))
            return false;
        throw std::runtime_error("Cannot lock worker ownership marker");
    }
#endif
    return true;
}
WorkerStageLease::WorkerStageLease(const std::filesystem::path& file, std::string_view marker)
    : WorkerStageLease() {
    if (marker.empty() || marker.size() > 512)
        throw std::runtime_error("Invalid worker ownership marker size");
    (void)open(file, true);
#ifdef _WIN32
    DWORD written = 0;
    if (!WriteFile(state_->handle, marker.data(), DWORD(marker.size()), &written, nullptr) ||
        written != marker.size() || !FlushFileBuffers(state_->handle))
        throw std::runtime_error("Cannot write worker ownership marker");
#else
    std::size_t offset = 0;
    while (offset < marker.size()) {
        const auto count = write(state_->handle, marker.data() + offset, marker.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("Cannot write worker ownership marker");
        offset += std::size_t(count);
    }
    if (fsync(state_->handle))
        throw std::runtime_error("Cannot flush worker ownership marker");
#endif
}
std::unique_ptr<WorkerStageLease> WorkerStageLease::try_open(const std::filesystem::path& file) {
    auto result = std::unique_ptr<WorkerStageLease>(new WorkerStageLease);
    if (!result->open(file, false))
        return {};
    return result;
}
std::string WorkerStageLease::marker() const {
    std::string result;
#ifdef _WIN32
    LARGE_INTEGER length{}, zero{};
    if (!GetFileSizeEx(state_->handle, &length) || length.QuadPart <= 0 || length.QuadPart > 512 ||
        !SetFilePointerEx(state_->handle, zero, nullptr, FILE_BEGIN))
        throw std::runtime_error("Invalid worker ownership marker length");
    result.resize(std::size_t(length.QuadPart));
    DWORD read = 0;
    if (!ReadFile(state_->handle, result.data(), DWORD(result.size()), &read, nullptr) ||
        read != result.size())
        throw std::runtime_error("Cannot read worker ownership marker");
#else
    struct stat info{};
    if (fstat(state_->handle, &info) || info.st_size <= 0 || info.st_size > 512)
        throw std::runtime_error("Invalid worker ownership marker length");
    result.resize(std::size_t(info.st_size));
    std::size_t offset = 0;
    while (offset < result.size()) {
        const auto count =
            pread(state_->handle, result.data() + offset, result.size() - offset, off_t(offset));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("Cannot read worker ownership marker");
        offset += std::size_t(count);
    }
#endif
    return result;
}
std::uintptr_t WorkerStageLease::inheritance_handle() const noexcept {
#ifdef _WIN32
    return reinterpret_cast<std::uintptr_t>(state_->handle);
#else
    return std::uintptr_t(state_->handle);
#endif
}
} // namespace forge::asset_detail
