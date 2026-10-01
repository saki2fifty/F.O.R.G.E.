#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <forge/project_paths.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
// Saved managed gameplay inputs only. Call at build/Play/export boundaries, never
// from the editor frame loop. Paths and contents both contribute to the identity.
inline std::string gameplay_source_digest(const std::filesystem::path& project) {
    const ProjectPaths paths(project);
    const auto root = paths.resolve("Native");
    if (!std::filesystem::is_directory(root))
        throw std::runtime_error("C++ gameplay source folder is missing");
    std::vector<std::filesystem::path> files;
    std::uint64_t total = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(root);
         it != std::filesystem::recursive_directory_iterator(); ++it) {
        const auto status = it->symlink_status();
        if (std::filesystem::is_symlink(status))
            throw std::runtime_error("C++ gameplay source contains a redirected path");
        if (std::filesystem::is_directory(status)) {
            if (it->path().filename() == "Builds" || it->path().filename() == ".forge")
                it.disable_recursion_pending();
            continue;
        }
        if (!std::filesystem::is_regular_file(status))
            throw std::runtime_error("C++ gameplay source contains a special file");
        // Managed Native/ is the build input tree. CMake can include arbitrary
        // files, so an extension whitelist would silently miss a changed .inl,
        // generated include or custom configuration input.
        if (files.size() >= 2048)
            throw std::runtime_error("C++ gameplay source file limit exceeded");
        files.push_back(paths.relative(it->path()));
    }
    std::sort(files.begin(), files.end(), ProjectLocatorLess{});
    std::string record = "forge.gameplay-source.v1\n";
    for (const auto& locator : files) {
        const auto path = paths.resolve(locator);
        const auto size = std::filesystem::file_size(path);
        if (size > 1024 * 1024 || total + size > 64ull * 1024 * 1024)
            throw std::runtime_error("C++ gameplay source byte limit exceeded");
        total += size;
        const auto bytes = asset_detail::read_bytes(path, 1024 * 1024);
        record += path_utf8(locator) + "\n" + asset_detail::content_digest(bytes) + "\n";
    }
    return asset_detail::content_digest(std::as_bytes(std::span(record)));
}
inline std::string gameplay_build_identity(const std::string& source_digest,
                                           const std::string& sdk_fingerprint) {
    const auto record = "forge.gameplay-build.v1\n" + source_digest + "\n" + sdk_fingerprint;
    return asset_detail::content_digest(std::as_bytes(std::span(record)));
}
inline std::string installed_gameplay_sdk_fingerprint(const std::filesystem::path& sdk) {
    std::ifstream input(sdk / "sdk/include/forge/native_sdk_identity.h");
    if (!input)
        throw std::runtime_error("Matching C++ Developer Kit is unavailable");
    constexpr std::string_view prefix = "#define FORGE_NATIVE_SDK_FINGERPRINT \"";
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.starts_with(prefix) && line.size() == prefix.size() + 65 && line.back() == '"') {
            auto value = line.substr(prefix.size(), 64);
            if (std::all_of(value.begin(), value.end(), [](unsigned char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }))
                return value;
        }
    }
    throw std::runtime_error("C++ Developer Kit has no valid SDK fingerprint");
}
inline bool gameplay_source_current(const std::filesystem::path& project,
                                    const nlohmann::json& settings,
                                    std::string_view expected_fingerprint = {}) {
    for (const auto& module : settings.value("modules", nlohmann::json::array()))
        if (module.is_object() && module.value("id", "") == "project.gameplay") {
            if (!module.contains("source_identity") ||
                (!expected_fingerprint.empty() && module.at("fingerprint") != expected_fingerprint))
                return false; // Older or mismatched SDK builds need one new build.
            return module.at("source_identity") ==
                   gameplay_build_identity(gameplay_source_digest(project),
                                           module.at("fingerprint").get<std::string>());
        }
    return false;
}
} // namespace forge
