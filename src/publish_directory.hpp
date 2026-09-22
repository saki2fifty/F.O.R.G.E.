#pragma once
#include "native_io_path.hpp"
#include <filesystem>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace forge::asset_detail {
inline void rename_new_directory(const std::filesystem::path& from,
                                 const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(native_io_path(from).c_str(), native_io_path(to).c_str(),
                     MOVEFILE_WRITE_THROUGH))
        throw std::filesystem::filesystem_error(
            "Cannot publish new directory", from, to,
            std::error_code(GetLastError(), std::system_category()));
#else
    // rename() may replace an existing empty directory on POSIX. NOREPLACE is
    // necessary even after preflight: another process may create the destination.
    if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE))
        throw std::filesystem::filesystem_error("Cannot publish new directory", from, to,
                                                std::error_code(errno, std::generic_category()));
#endif
}
} // namespace forge::asset_detail
