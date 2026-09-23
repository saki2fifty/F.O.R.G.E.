#pragma once
#include <forge/assets.hpp>
#include <forge/project_lease.hpp>
#include <span>
namespace forge {
struct UiAssetSnapshot;
inline bool declared_runtime_edge(const AssetDependency& edge) {
    return is_declared_runtime_dependency(edge);
}
// Required finite sets. Reason/group belongs to each existing typed graph edge;
// metadata stores only reviewed source revisions, never a duplicate asset list.
// Optional Content discoveries admit requested Scene/Prefab documents into the
// same candidate; expected_catalog always means the persisted catalog revision.
AssetCatalog declare_runtime_dependencies(const ProjectLease&, AssetId owner,
                                          std::vector<AssetDependency>,
                                          const nlohmann::json& expected_catalog,
                                          const UiAssetSnapshot* prepared_ui = nullptr,
                                          std::span<const AssetRecord> discovered_documents = {});
void validate_runtime_declarations(const std::filesystem::path&, const AssetRecord&);
} // namespace forge
