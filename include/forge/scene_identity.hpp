#pragma once
#include <filesystem>
#include <forge/identity.hpp>
namespace forge {
using Json = nlohmann::json;
Json empty_scene();
// Pure conversion. Existing mapping is only used for edits within the same legacy document.
Json migrate_scene(const Json& source, const Json* existing = nullptr);
Json duplicate_scene_asset(const Json& source);
// Use a preallocated NEW asset identity while remapping known intra-scene refs.
Json duplicate_scene_asset(const Json& source, AssetId destination);
std::string resolve_legacy_id(const Json& document, const std::string& id);
// A companion record retains assignments without overwriting a legacy source on open.
Json read_scene_file(const std::filesystem::path& path);
void write_scene_file(const std::filesystem::path& path, const Json& document);
} // namespace forge
