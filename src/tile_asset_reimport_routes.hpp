#pragma once
#include "asset_reimport.hpp"
#include "tile_asset_authoring.hpp"

namespace forge {
// Compose the existing registry, target and publication owners without changing
// native tile recipes or introducing another catalog/import service.
inline std::vector<AssetReimportRoute> tile_asset_reimport_routes(std::filesystem::path worker,
                                                                  ImportTarget target) {
    const auto registry = tile_asset_import_registry(std::move(worker));
    return {{tile_set_importer_id, target, registry,
             [](auto& candidate, const auto& plan, const auto&) {
                 prepare_tile_set_publication(candidate, plan);
             }},
            {tile_map_importer_id, std::move(target), registry,
             [](auto& candidate, const auto& plan, const auto&) {
                 prepare_tile_map_publication(candidate, plan);
             }}};
}
} // namespace forge
