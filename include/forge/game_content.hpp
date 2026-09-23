#pragma once
#include <forge/assets.hpp>
namespace forge {
// Read-only bootstrap: exact AssetId, bounded current scene document plus the
// transitive structured prefab sources needed by Scene::restore_snapshot.
// No migration, directory scan, import, cache generation or authored file writes.
nlohmann::json load_game_scene(const std::filesystem::path& root, AssetRef<SceneAsset>);
} // namespace forge
