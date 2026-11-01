#pragma once
#include "sprite_atlas_source.hpp"
#include <forge/asset_importer.hpp>
#include <forge/asset_publication.hpp>

namespace forge::asset_detail {
std::shared_ptr<const AssetImporter> sprite_atlas_importer(std::filesystem::path worker);
}
namespace forge {
void prepare_sprite_atlas_publication(AssetPublicationCandidate&, const AssetImportPlan&);
}
