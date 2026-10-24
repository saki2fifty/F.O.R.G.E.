#pragma once
#include <filesystem>
#include <system_error>

namespace forge::test {
// Python and the native fixture share only these disposable control files.
// Windows may see a named file before the creating/deleting handle is closed.
inline bool marker_busy(const std::error_code& error) {
#ifdef _WIN32
    return error.category() == std::system_category() &&
           (error.value() == 5 || error.value() == 32 || error.value() == 33);
#else
    (void)error;
    return false;
#endif
}
inline bool marker_present(const std::filesystem::path& path) {
    std::error_code error;
    const bool present = std::filesystem::exists(path, error);
    if (!error)
        return present;
    if (marker_busy(error))
        return true; // Keep the current hold/gesture until absence is confirmed.
    throw std::filesystem::filesystem_error("Read MCP fixture control", path, error);
}
inline bool consume_marker(const std::filesystem::path& path) {
    std::error_code error;
    const bool consumed = std::filesystem::remove(path, error);
    if (error && !marker_busy(error))
        throw std::filesystem::filesystem_error("Consume MCP fixture control", path, error);
    return !error && consumed; // A busy file stays pending; never repeat its action.
}
} // namespace forge::test
