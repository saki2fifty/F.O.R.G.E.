#pragma once
#include <forge/assets.hpp>
#include <forge/project_paths.hpp>
#include <forge/scene.hpp>

namespace forge::detail {
// Shared schema admission for explicit registration and read-only discovery.
// Catalog insertion still owns project locator, identity and graph validation.
inline AssetRecord scene_asset_record(const std::filesystem::path& source, const Json& doc) {
    Scene::validate_document(doc);
    if (doc.at("version") != 2 && !Scene::supports_saved_version(doc.at("version").get<unsigned>()))
        throw std::runtime_error("Scene must be migrated before catalog registration");
    return {doc.at("asset_id").get<AssetId>(),
            SceneAsset::type,
            ProjectPaths::normalize(source),
            doc.at("version").get<unsigned>(),
            {}};
}
} // namespace forge::detail
