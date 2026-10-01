#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <forge/project_paths.hpp>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
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
        const auto name = it->path().filename().string();
        const auto ext = it->path().extension().string();
        if (name != "CMakeLists.txt" && name != "forge.sdk-project.json" && ext != ".cmake" &&
            ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".h" && ext != ".hpp")
            continue;
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
inline bool gameplay_source_current(const std::filesystem::path& project,
                                    const nlohmann::json& settings) {
    for (const auto& module : settings.value("modules", nlohmann::json::array()))
        if (module.is_object() && module.value("id", "") == "project.gameplay") {
            if (!module.contains("source_identity"))
                return false; // Older managed builds need one new build.
            return module.at("source_identity") ==
                   gameplay_build_identity(gameplay_source_digest(project),
                                           module.at("fingerprint").get<std::string>());
        }
    return false;
}
} // namespace forge
