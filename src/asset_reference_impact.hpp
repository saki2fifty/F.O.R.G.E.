#pragma once
#include <forge/asset_discovery.hpp>
#include <forge/assets.hpp>
#include <set>
#include <span>
namespace forge {
struct AssetReferenceHit {
    std::filesystem::path source;
    std::string owner, property, kind;
    AssetId target;
    std::string expected_type;
};
struct AssetReferenceImpact {
    std::vector<AssetReferenceHit> references;
    std::vector<std::string> uninspected;
    std::map<std::filesystem::path, std::string> reviewed_sources;
};
struct AssetReferenceDocument {
    std::filesystem::path source;
    nlohmann::json document;
};
// Detached native Meta projection only; never searches/replaces arbitrary UUID strings.
AssetReferenceImpact inspect_asset_references(const nlohmann::json& schema,
                                              std::span<const AssetReferenceDocument>,
                                              const std::set<AssetId>& targets);
// All reflected references, including unresolved identities. Same inspector and
// opaque-field diagnostics as impact review; no catalog membership filter.
AssetReferenceImpact collect_asset_references(const nlohmann::json& schema,
                                              std::span<const AssetReferenceDocument>);
// Worker-only bounded scan, including unopened sources and the current detached draft.
AssetReferenceImpact scan_asset_references(const std::filesystem::path& project,
                                           const nlohmann::json& schema,
                                           const std::set<AssetId>& targets,
                                           std::span<const AssetReferenceDocument> drafts = {},
                                           std::stop_token stop = {});
} // namespace forge
