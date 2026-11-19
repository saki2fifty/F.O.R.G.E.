#pragma once
#ifdef _WIN32
#include "native_io_path.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#include <windows.h>
// Shared Win32 namespace replacement; callers retain staging, validation,
// durability, writer scope and cleanup ownership. Unsupported capabilities alone
// return false for the existing MoveFileEx fallback. Other failures remain errors.
namespace forge::asset_detail {
inline bool replace_open_file(HANDLE staged, const std::filesystem::path& from,
                              const std::filesystem::path& to) {
#ifdef FILE_RENAME_FLAG_POSIX_SEMANTICS
    const auto target = native_io_path(to).native();
    constexpr auto fixed_bytes = offsetof(FILE_RENAME_INFO, FileName) + sizeof(wchar_t);
    if (target.size() > (MAXDWORD - fixed_bytes) / sizeof(wchar_t))
        throw std::runtime_error("File destination exceeds Windows rename limits");
    const auto name_bytes = target.size() * sizeof(wchar_t);
    const auto packet_bytes = fixed_bytes + name_bytes;
    std::unique_ptr<void, decltype(&std::free)> memory(std::calloc(1, packet_bytes), &std::free);
    if (!memory)
        throw std::bad_alloc();
    auto* info = static_cast<FILE_RENAME_INFO*>(memory.get());
    info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
    info->FileNameLength = DWORD(name_bytes);
    std::memcpy(static_cast<std::byte*>(memory.get()) + offsetof(FILE_RENAME_INFO, FileName),
                target.data(), name_bytes);
    if (SetFileInformationByHandle(staged, FileRenameInfoEx, info, DWORD(packet_bytes)))
        return true;
    const auto error = GetLastError();
    // Preserve the previous platform path where this information class or
    // filesystem capability is unavailable. Other failures are real failures.
    if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_SUPPORTED &&
        error != ERROR_INVALID_FUNCTION)
        throw std::filesystem::filesystem_error("Cannot replace file", from, to,
                                                std::error_code(error, std::system_category()));
#else
    (void)staged;
    (void)from;
    (void)to;
#endif
    return false;
}
} // namespace forge::asset_detail
#endif
