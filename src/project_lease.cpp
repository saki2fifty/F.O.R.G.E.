#include <forge/project_lease.hpp>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace forge {
struct ProjectLease::State {
    std::filesystem::path folder, file;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~State() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
#else
    int handle = -1;
    ~State() {
        if (handle >= 0)
            close(handle);
    }
#endif
};
ProjectLease::ProjectLease(const std::filesystem::path& root) : state_(std::make_unique<State>()) {
    state_->folder = std::filesystem::canonical(root) / ".forge";
    std::filesystem::create_directory(state_->folder);
    if (std::filesystem::is_symlink(state_->folder) ||
        std::filesystem::weakly_canonical(state_->folder) != state_->folder)
        throw std::runtime_error("Project .forge folder must not redirect outside its location");
    state_->file = state_->folder / "writer.lock";
#ifdef _WIN32
    state_->handle =
        CreateFileW(state_->file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (state_->handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION)
            throw std::runtime_error(
                "Project is already open in another FORGE editor. Close that editor first.");
        throw std::runtime_error("Cannot acquire project writer ownership (Windows error " +
                                 std::to_string(error) + ")");
    }
#else
    state_->handle = open(state_->file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (state_->handle < 0 || flock(state_->handle, LOCK_EX | LOCK_NB) != 0)
        throw std::runtime_error("Cannot acquire project writer ownership. Another FORGE editor "
                                 "may have this project open; check folder permissions.");
#endif
    check();
}
ProjectLease::~ProjectLease() = default;
std::filesystem::path ProjectLease::root() const { return state_->folder.parent_path(); }
void ProjectLease::check() const {
    if (std::filesystem::weakly_canonical(state_->folder) != state_->folder)
        throw std::runtime_error("Project control folder moved; reopen the project before writing");
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(state_->handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        throw std::runtime_error("Invalid project writer lock; reopen the project");
#else
    struct stat held{}, current{};
    if (fstat(state_->handle, &held) || lstat(state_->file.c_str(), &current) ||
        !S_ISREG(current.st_mode) || held.st_dev != current.st_dev || held.st_ino != current.st_ino)
        throw std::runtime_error(
            "Project writer lock was removed or replaced; reopen the project before writing");
#endif
}
} // namespace forge
