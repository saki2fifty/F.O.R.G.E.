#pragma once
#include <filesystem>

namespace forge {

// Packaged Windows tools live under bin/. Source-tree builds keep the legacy
// executable-relative layout. The package manifest distinguishes the two.
inline std::filesystem::path installation_root(const std::filesystem::path& executable_folder) {
    const auto folder =
        executable_folder.filename().empty() ? executable_folder.parent_path() : executable_folder;
    const auto parent = folder.parent_path();
    if (folder.filename() == "bin" && std::filesystem::is_regular_file(parent / "manifest.json"))
        return parent;
    return folder;
}

} // namespace forge
