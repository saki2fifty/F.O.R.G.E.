#pragma once
#include <filesystem>
namespace forge::asset_detail {
// An OS-call argument only: never serialize this spelling or use it as logical
// asset identity. C++ file streams do not automatically opt into Win32 long paths.
inline std::filesystem::path native_io_path(const std::filesystem::path& path) {
#ifdef _WIN32
    auto absolute = std::filesystem::absolute(path).lexically_normal();
    absolute.make_preferred();
    auto value = absolute.native();
    if (value.starts_with(L"\\\\?\\") || value.starts_with(L"\\\\.\\"))
        return absolute;
    if (value.starts_with(L"\\\\"))
        return std::filesystem::path(L"\\\\?\\UNC\\" + value.substr(2));
    return std::filesystem::path(L"\\\\?\\" + value);
#else
    return path;
#endif
}
} // namespace forge::asset_detail
