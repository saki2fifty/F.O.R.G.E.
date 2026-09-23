#pragma once
#include "asset_import_service.hpp"
#include <forge/collision_source.hpp>
namespace forge {
inline ImportTarget collision_import_target() {
#ifdef _WIN32
    return {"windows", "none", "cpu"};
#else
    return {"linux", "none", "cpu"};
#endif
}

std::shared_ptr<const AssetImporterRegistry> collision_import_registry();
void prepare_collision_publication(AssetPublicationCandidate&, const AssetImportPlan&);
} // namespace forge
