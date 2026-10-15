#pragma once
#include <filesystem>
#include <nlohmann/json.hpp>
namespace forge::detail {
// Keep the caller's scene edits/explicit definitions and admit additional
// code-selected Prefabs through the existing project's typed Runtime graph.
// Read-only preparation, no world/registry, import, catalog write or scene reload.
nlohmann::json runtime_scene_content(const std::filesystem::path&, const nlohmann::json& snapshot);
} // namespace forge::detail
