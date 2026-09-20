#include <algorithm>
#include <forge/project_paths.hpp>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif
namespace forge {
std::string path_utf8(const std::filesystem::path& path) {
    const auto s = path.generic_u8string();
    return {s.begin(), s.end()};
}
bool ProjectLocatorLess::operator()(const std::filesystem::path& a,
                                    const std::filesystem::path& b) const {
#ifdef _WIN32
    const auto comparison = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
    if (!comparison)
        throw std::runtime_error("Cannot compare project locators");
    return comparison == CSTR_LESS_THAN;
#else
    return a < b;
#endif
}
ProjectPaths::ProjectPaths(std::filesystem::path root)
    : root_(std::filesystem::weakly_canonical(std::filesystem::absolute(root))) {}
std::filesystem::path ProjectPaths::normalize(const std::filesystem::path& source) {
    auto text = path_utf8(source);
    std::replace(text.begin(), text.end(), '\\', '/');
    if (text.empty() || text.front() == '/' || text.find(':') != std::string::npos ||
        text.find('\0') != std::string::npos)
        throw std::runtime_error("Expected a project-relative locator");
    auto path = std::filesystem::u8path(text).lexically_normal();
    if (path.empty() || path == "." || path.is_absolute() || path.has_root_name() ||
        *path.begin() == "..")
        throw std::runtime_error("Project locator escapes root or names the root");
    if (!path.has_filename())
        path = path.parent_path();
    for (const auto& part : path) {
        auto s = path_utf8(part);
        auto device = s.substr(0, s.find('.'));
        std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) {
            return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : char(c);
        });
        if (device == "CON" || device == "PRN" || device == "AUX" || device == "NUL" ||
            device == "CONIN$" || device == "CONOUT$" ||
            (device.size() == 4 && (device.starts_with("COM") || device.starts_with("LPT")) &&
             device[3] >= '1' && device[3] <= '9'))
            throw std::runtime_error("Reserved device name in project locator");
        if (s.empty() || s.back() == '.' || s.back() == ' ' ||
            s.find_first_of("<>\"|?*") != std::string::npos)
            throw std::runtime_error("Nonportable project locator component");
    }
    return path;
}
std::filesystem::path ProjectPaths::relative(const std::filesystem::path& absolute) const {
    const auto resolved = std::filesystem::weakly_canonical(absolute);
    auto r = root_.begin(), p = resolved.begin();
    for (; r != root_.end(); ++r, ++p) {
        if (p == resolved.end())
            throw std::runtime_error("Path escapes project root");
#ifdef _WIN32
        if (CompareStringOrdinal(r->c_str(), -1, p->c_str(), -1, TRUE) != CSTR_EQUAL)
#else
        if (*r != *p)
#endif
            throw std::runtime_error("Path escapes project root");
    }
    std::filesystem::path relative;
    for (; p != resolved.end(); ++p)
        relative /= *p;
    if (relative.empty())
        throw std::runtime_error("Path names project root");
    return normalize(relative);
}
std::filesystem::path ProjectPaths::resolve(const std::filesystem::path& locator) const {
    const auto path = std::filesystem::weakly_canonical(root_ / normalize(locator));
    (void)relative(path);
    return path;
}
std::string ProjectPaths::file_identity(const std::filesystem::path& locator) const {
    const auto path = resolve(locator);
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot inspect source identity (OS error " +
                                 std::to_string(GetLastError()) + ")");
    FILE_ID_INFO info{};
    const auto ok = GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info));
    const auto error = GetLastError();
    CloseHandle(handle);
    if (!ok)
        throw std::runtime_error("Cannot inspect source identity (OS error " +
                                 std::to_string(error) + ")");
    auto result = std::to_string(info.VolumeSerialNumber) + ":";
    constexpr char hex[] = "0123456789abcdef";
    for (auto byte : info.FileId.Identifier) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
#else
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0)
        throw std::runtime_error("Cannot inspect source identity");
    return std::to_string(info.st_dev) + ":" + std::to_string(info.st_ino);
#endif
}
bool ProjectPaths::same_locator(const std::filesystem::path& a,
                                const std::filesystem::path& b) const {
    const auto left = resolve(a), right = resolve(b);
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error) && !error)
        return true;
#ifdef _WIN32
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}
} // namespace forge
