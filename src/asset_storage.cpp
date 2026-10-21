#include "asset_storage.hpp"
#include "asset_bytes.hpp"
#include "native_io_path.hpp"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <forge/asset_ref.hpp>
#include <memory>
#include <new>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace forge::asset_storage {
#ifdef _WIN32
namespace {
bool replace_open_file(HANDLE staged, const std::filesystem::path& from,
                       const std::filesystem::path& to) {
#ifdef FILE_RENAME_FLAG_POSIX_SEMANTICS
    const auto target = asset_detail::native_io_path(to).native();
    constexpr auto fixed_bytes = offsetof(FILE_RENAME_INFO, FileName) + sizeof(wchar_t);
    if (target.size() > (MAXDWORD - fixed_bytes) / sizeof(wchar_t))
        throw std::runtime_error("Asset metadata destination exceeds Windows rename limits");
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
        throw std::filesystem::filesystem_error("Cannot replace asset metadata", from, to,
                                                std::error_code(error, std::system_category()));
#else
    (void)staged;
    (void)from;
    (void)to;
#endif
    return false;
}
} // namespace
#endif
void ordinary(const std::filesystem::path& path) {
    if (std::filesystem::weakly_canonical(path) != path || std::filesystem::is_symlink(path))
        throw std::runtime_error("Asset publication metadata must not redirect: " +
                                 path_utf8(path));
}
std::optional<std::string> read(const std::filesystem::path& path, std::size_t limit) {
    ordinary(path);
    if (!std::filesystem::exists(path))
        return {};
    const auto bytes = asset_detail::read_bytes(path, limit);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
void sync_directory(const std::filesystem::path& path) {
#ifndef _WIN32
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        throw std::runtime_error("Cannot open asset publication directory for flush");
    const auto error = fsync(fd) ? errno : 0;
    close(fd);
    if (error)
        throw std::filesystem::filesystem_error("Asset publication directory flush failed", path,
                                                std::error_code(error, std::generic_category()));
#else
    (void)path; // File/metadata flush and platform replacement below.
#endif
}
void replace(const std::filesystem::path& path, std::string_view bytes) {
    ordinary(path);
    // A unique sibling is sufficient for same-filesystem atomic replacement.
    // Appending to the full destination filename can exceed Windows MAX_PATH or
    // POSIX NAME_MAX even when the destination itself is valid.
    const auto temp = path.parent_path() / ("." + AssetId::generate().str() + ".pending");
    try {
#ifdef _WIN32
        HANDLE file = CreateFileW(asset_detail::native_io_path(temp).c_str(),
                                  GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::filesystem::filesystem_error(
                "Cannot stage asset metadata", temp,
                std::error_code(GetLastError(), std::system_category()));
        std::unique_ptr<void, decltype(&CloseHandle)> staged(file, &CloseHandle);
        DWORD written = 0;
        const bool ok =
            bytes.size() <= MAXDWORD &&
            WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
            written == bytes.size() && FlushFileBuffers(file);
        const auto error = ok ? 0 : GetLastError();
        if (!ok)
            throw std::filesystem::filesystem_error(
                "Cannot flush staged asset metadata", temp,
                std::error_code(error ? error : ERROR_WRITE_FAULT, std::system_category()));
        if (replace_open_file(file, temp, path)) {
            // Flush through the same writable handle after the namespace change,
            // keeping the existing data/metadata durability boundary.
            if (!FlushFileBuffers(file))
                throw std::filesystem::filesystem_error(
                    "Cannot flush replaced asset metadata", path,
                    std::error_code(GetLastError(), std::system_category()));
        } else {
            staged.reset(); // The legacy move requires its source handle closed.
            if (!MoveFileExW(asset_detail::native_io_path(temp).c_str(),
                             asset_detail::native_io_path(path).c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::filesystem::filesystem_error(
                    "Cannot replace asset metadata", temp, path,
                    std::error_code(GetLastError(), std::system_category()));
        }
#else
        const int fd =
            open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0)
            throw std::filesystem::filesystem_error(
                "Cannot stage asset metadata", temp,
                std::error_code(errno, std::generic_category()));
        std::size_t offset = 0;
        int error = 0;
        while (offset < bytes.size()) {
            const auto count = write(fd, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                error = count == 0 ? EIO : errno;
                break;
            }
            offset += static_cast<std::size_t>(count);
        }
        if (!error && fsync(fd))
            error = errno;
        if (close(fd) && !error)
            error = errno;
        if (error)
            throw std::filesystem::filesystem_error(
                "Cannot flush staged asset metadata", temp,
                std::error_code(error, std::generic_category()));
        std::filesystem::rename(temp, path);
        sync_directory(path.parent_path());
#endif
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
void erase_file(const std::filesystem::path& path) {
    ordinary(path);
    std::filesystem::remove(path);
    sync_directory(path.parent_path());
}
} // namespace forge::asset_storage
