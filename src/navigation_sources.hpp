#pragma once
#include "navigation_geometry.hpp"
#include <forge/scene.hpp>

namespace forge::navigation_detail {
// Copies only navigation inputs from this Scene instance. The existing transform
// owner evaluates matrices, and the shared bake validator supplies source rules.
Sources capture_native(WorldContext&, const Scene&, const flecs::query<const PersistentEntityId>&);
// Selected catalog provenance only; never loads Mesh bytes or changes selection.
std::string selected_geometry_digest(const std::filesystem::path&, const Sources&,
                                     const nlohmann::json& baked_metadata);
std::string selected_geometry_digest(const AssetCatalog&, const Sources&,
                                     const nlohmann::json& baked_metadata);
} // namespace forge::navigation_detail
