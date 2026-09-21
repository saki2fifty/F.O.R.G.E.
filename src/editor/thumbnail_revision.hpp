#pragma once
#include <forge/assets.hpp>
#include <stop_token>
namespace forge {
// Disposable preview identity, never a persistent AssetId or cooked asset format.
// Walk published dependencies on a worker. Files are admitted by the normal
// resource path later; this digest is only a cache invalidation hint.
inline std::string thumbnail_revision(const AssetCatalog& catalog, AssetId asset,
                                      std::stop_token stop = {}) {
    std::set<AssetId> remaining{asset}, seen;
    nlohmann::json records = nlohmann::json::array();
    while (!remaining.empty()) {
        if (stop.stop_requested())
            throw std::runtime_error("Thumbnail revision preparation cancelled");
        const auto id = *remaining.begin();
        remaining.erase(remaining.begin());
        if (!seen.insert(id).second)
            continue;
        if (seen.size() > 16384)
            throw std::runtime_error("Thumbnail dependency closure exceeds 16384 assets");
        const auto found = catalog.records().find(id);
        if (found == catalog.records().end()) {
            records.push_back({{"id", id}, {"missing", true}});
            continue;
        }
        const auto& record = found->second;
        records.push_back({{"id", id},
                           {"type", record.type},
                           {"schema", record.schema_version},
                           {"receipt", record.metadata.value("forge.import", nlohmann::json{})},
                           {"dependencies", record.dependencies},
                           {"edges", record.dependency_edges},
                           {"removed", record.subasset && record.subasset->removed}});
        remaining.insert(record.dependencies.begin(), record.dependencies.end());
        if (record.subasset)
            remaining.insert(record.subasset->owner);
        if (remaining.size() > 16384)
            throw std::runtime_error("Thumbnail dependency frontier exceeds 16384 assets");
    }
    return asset_build_digest({{"preview_recipe", 1}, {"asset", asset}, {"records", records}});
}
} // namespace forge
