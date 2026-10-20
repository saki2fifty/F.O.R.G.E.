#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "native_io_path.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <windows.h>

namespace forge::asset_detail {
// A reader pins its opened file revision while allowing the existing atomic
// publisher to replace the directory entry. No borrowed handle escapes.
class NativeReadFile {
  public:
    explicit NativeReadFile(const std::filesystem::path& path)
        : handle_(CreateFileW(native_io_path(path).c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              nullptr)) {
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot read asset input: " + path.string());
    }
    ~NativeReadFile() { CloseHandle(handle_); }
    NativeReadFile(const NativeReadFile&) = delete;
    NativeReadFile& operator=(const NativeReadFile&) = delete;
    std::vector<std::byte> read(std::size_t limit) {
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(handle_, &length) || length.QuadPart < 0 ||
            std::uint64_t(length.QuadPart) > limit)
            throw std::runtime_error("Asset input exceeds byte limit");
        std::vector<std::byte> bytes(static_cast<std::size_t>(length.QuadPart));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto requested = DWORD(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
            DWORD count = 0;
            if (!ReadFile(handle_, bytes.data() + offset, requested, &count, nullptr) || !count)
                throw std::runtime_error("Asset input changed or was truncated while reading");
            offset += count;
        }
        std::byte extra{};
        DWORD count = 0;
        if (!ReadFile(handle_, &extra, 1, &count, nullptr) || count)
            throw std::runtime_error("Asset input changed or was truncated while reading");
        return bytes;
    }

  private:
    HANDLE handle_;
};
} // namespace forge::asset_detail
#endif
