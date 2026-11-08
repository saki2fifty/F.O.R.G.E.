#pragma once
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include <forge/input.hpp>
#include <forge/project_paths.hpp>

namespace forge {
// The caller owns the project/output grant. Use the established contained path
// and durable file primitives; recordings are ordinary files, not asset records.
inline std::filesystem::path input_recording_path(const std::filesystem::path& root,
                                                  const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.extension() != ".json")
        throw std::runtime_error("Choose a relative .json input-recording path");
    const ProjectPaths paths(root);
    if (paths.root() != root)
        throw std::runtime_error("The input-recording project folder changed");
    const auto normalized = ProjectPaths::normalize(relative);
    if (std::distance(normalized.begin(), normalized.end()) != 2 ||
        *normalized.begin() != "Recordings")
        throw std::runtime_error("Use Recordings/<name>.json inside this project");
    asset_storage::ordinary(root / normalized);
    const auto path = paths.resolve(normalized);
    asset_storage::ordinary(path);
    return path;
}
inline nlohmann::json read_input_recording(const std::filesystem::path& root,
                                           const std::filesystem::path& relative) {
    const auto text =
        asset_storage::read(input_recording_path(root, relative), input_recording_byte_limit);
    if (!text)
        throw std::runtime_error("Input recording file does not exist");
    auto document = asset_detail::parse_bounded_json(std::as_bytes(std::span(*text)),
                                                     input_recording_byte_limit);
    validate_input_recording(document);
    return document;
}
inline void write_input_recording(const std::filesystem::path& root,
                                  const std::filesystem::path& relative,
                                  const nlohmann::json& document, bool replace) {
    validate_input_recording(document);
    const auto path = input_recording_path(root, relative);
    std::filesystem::create_directory(path.parent_path());
    if (!replace && std::filesystem::exists(path))
        throw std::runtime_error("The recording exists; explicitly allow replacement");
    asset_storage::replace(path, document.dump());
}
} // namespace forge
