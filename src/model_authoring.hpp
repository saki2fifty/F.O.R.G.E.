#pragma once
#include <forge/asset_publication.hpp>
namespace forge {
// Detached complete-family preparation, shared by editor and headless service.
// Throws SubassetIdentityFailure with bounded structured alternatives on ambiguity.
// It neither writes files nor changes the input candidate on failure.
void prepare_model_publication(AssetPublicationCandidate& candidate, const AssetImportPlan& plan,
                               std::span<const SubassetIdentityDecision> decisions = {});
} // namespace forge
