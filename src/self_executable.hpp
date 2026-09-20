#pragma once
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace forge {
inline std::filesystem::path self_executable() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (!count || count >= buffer.size())
        throw std::runtime_error("Cannot resolve the running executable path");
    buffer.resize(count);
    return std::filesystem::path(buffer);
#else
    return std::filesystem::read_symlink("/proc/self/exe");
#endif
}
} // namespace forge
