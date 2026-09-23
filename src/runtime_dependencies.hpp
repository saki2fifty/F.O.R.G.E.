#pragma once
#include <forge/assets.hpp>
#include <forge/project_lease.hpp>
namespace forge {
struct UiAssetSnapshot;
inline bool declared_runtime_edge(const AssetDependency& edge) {
    return is_declared_runtime_dependency(edge);
}
// Required finite sets. Reason/group belongs to each existing typed graph edge;
// metadata stores only reviewed source revisions, never a duplicate asset list.
AssetCatalog declare_runtime_dependencies(const ProjectLease&, AssetId owner,
                                          std::vector<AssetDependency>,
                                          const nlohmann::json& expected_catalog,
                                          const UiAssetSnapshot* prepared_ui = nullptr);
void validate_runtime_declarations(const std::filesystem::path&, const AssetRecord&);
} // namespace forge
