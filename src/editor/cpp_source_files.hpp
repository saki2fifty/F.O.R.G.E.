#pragma once
#include "../asset_storage.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <forge/project_paths.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <vector>

namespace forge::ui {
inline constexpr std::size_t max_cpp_source_bytes = 1024 * 1024;
inline bool cpp_source_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return extension == ".cpp" || extension == ".hpp" || extension == ".h" || extension == ".cc" ||
           extension == ".cxx";
}
inline std::filesystem::path cpp_source_path(const std::filesystem::path& root,
                                             const std::filesystem::path& locator) {
    auto normalized = ProjectPaths::normalize(locator);
    if (*normalized.begin() != "Native" || !cpp_source_extension(normalized))
        throw std::runtime_error("Choose a C++ source/header inside Native.");
    for (const auto& part : normalized)
        if (part == "Builds" || part == ".forge")
            throw std::runtime_error("Generated deployment/build files are not editable sources.");
    auto path = ProjectPaths(root).resolve(normalized);
    asset_storage::ordinary(path);
    return path;
}
inline void validate_cpp_text(const std::string& text) {
    if (text.size() > max_cpp_source_bytes || text.find('\0') != std::string::npos)
        throw std::runtime_error("C++ source must be UTF-8 text without NUL, at most 1 MiB.");
    (void)nlohmann::json(text).dump(); // Reuse JSON's strict UTF-8 validation.
}
inline std::string read_cpp_source(const std::filesystem::path& path) {
    auto text = asset_storage::read(path, max_cpp_source_bytes);
    if (!text)
        throw std::runtime_error("C++ source no longer exists.");
    validate_cpp_text(*text);
    return *text;
}
inline void save_cpp_source(const std::filesystem::path& root, const std::filesystem::path& locator,
                            const std::string& baseline, const std::string& text) {
    validate_cpp_text(text);
    const auto path = cpp_source_path(root, locator);
    if (read_cpp_source(path) != baseline)
        throw std::runtime_error(
            "Source changed outside FORGE. Reload before saving; your draft is retained.");
    asset_storage::replace(path, text);
}
inline void create_cpp_source(const std::filesystem::path& root, const std::string& name) {
    if (name.empty() || name.size() > 120 ||
        name.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") !=
            std::string::npos ||
        (std::filesystem::path(name).extension() != ".cpp" &&
         std::filesystem::path(name).extension() != ".hpp"))
        throw std::runtime_error("Use a simple filename ending in .cpp or .hpp.");
    const auto path = cpp_source_path(root, std::filesystem::path("Native") / name);
    if (std::filesystem::exists(path))
        throw std::runtime_error("That source already exists; it was not overwritten.");
    const auto cmake = ProjectPaths(root).resolve("Native/CMakeLists.txt");
    const auto original = asset_storage::read(cmake);
    if (!original || !std::filesystem::is_regular_file(root / "Native/forge.sdk-project.json"))
        throw std::runtime_error("Create a managed C++ gameplay project first.");
    const std::string include = "include(forge.sources.cmake OPTIONAL)";
    if (original->find(include) == std::string::npos) {
        // Only upgrade the exact FORGE starter, never a user-modified CMake recipe.
        const std::string legacy =
            "cmake_minimum_required(VERSION 3.24)\nproject(ForgeGameplay LANGUAGES "
            "CXX)\nfind_package(ForgeNativeSdk CONFIG REQUIRED)\nadd_library(gameplay MODULE "
            "gameplay.cpp)\ntarget_link_libraries(gameplay PRIVATE "
            "ForgeNativeSdk::Client)\nset_target_properties(gameplay PROPERTIES PREFIX \"\" "
            "BUILD_RPATH_USE_ORIGIN ON)\nforge_install_gameplay_runtime(gameplay NAME "
            "project.gameplay)\n";
        auto normalized = *original;
        std::erase(normalized, '\r');
        if (normalized != legacy)
            throw std::runtime_error(
                "Custom CMake recipe: add include(forge.sources.cmake OPTIONAL) after "
                "add_library(gameplay ...) before creating additional sources.");
        const auto position = normalized.find("target_link_libraries");
        normalized.insert(position, include + "\n");
        asset_storage::replace(cmake, normalized);
    }
    const auto manifest = ProjectPaths(root).resolve("Native/forge.sources.cmake");
    asset_storage::ordinary(manifest);
    auto entries =
        asset_storage::read(manifest).value_or("# FORGE-created additional source files.\n");
    if (name.ends_with(".cpp"))
        entries += "target_sources(gameplay PRIVATE \"${CMAKE_CURRENT_LIST_DIR}/" + name + "\")\n";
    // File first; a failed registration removes only our unchanged newly created file.
    const std::string initial =
        name.ends_with(".hpp") ? "#pragma once\n" : "// Project gameplay source.\n";
    asset_storage::replace(path, initial);
    try {
        if (name.ends_with(".cpp"))
            asset_storage::replace(manifest, entries);
    } catch (...) {
        if (asset_storage::read(path).value_or("") == initial)
            asset_storage::erase_file(path);
        throw;
    }
}
struct CppDiagnostic {
    std::string source, message;
    int line = 0, column = 1;
};
inline std::vector<CppDiagnostic> cpp_diagnostics(const std::filesystem::path& root,
                                                  const std::string& log) {
    static const std::regex msvc(R"(^(.+)\(([0-9]+)(?:,([0-9]+))?\)\s*:\s*(.*)$)");
    static const std::regex clang(R"(^(.+?):([0-9]+)(?::([0-9]+))?:\s*(.*)$)");
    std::vector<CppDiagnostic> result;
    std::istringstream lines(log);
    std::string line;
    while (result.size() < 256 && std::getline(lines, line)) {
        if (line.size() > 8192 ||
            (line.find("error") == std::string::npos && line.find("warning") == std::string::npos))
            continue;
        std::smatch match;
        if (!std::regex_match(line, match, msvc) && !std::regex_match(line, match, clang))
            continue;
        auto integer = [](const std::string& value) {
            int number = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                           number > 0
                       ? number
                       : 0;
        };
        try {
            const auto path = std::filesystem::u8path(match[1].str());
            const auto locator = path.is_absolute() ? ProjectPaths(root).relative(path)
                                                    : ProjectPaths::normalize(path);
            (void)cpp_source_path(root, locator);
            const int row = integer(match[2].str());
            const int column = match[3].matched ? integer(match[3].str()) : 1;
            if (row && column)
                result.push_back({path_utf8(locator), match[4].str(), row, column});
        } catch (const std::exception&) {
            // SDK/generated/external diagnostics remain visible in raw build output.
        }
    }
    return result;
}
} // namespace forge::ui
