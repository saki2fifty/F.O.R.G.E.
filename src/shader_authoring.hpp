#pragma once
#include <forge/asset_publication.hpp>
namespace forge {
// Shared cache-hit/fresh-output publication check; does not mutate live GPU state.
void prepare_shader_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
