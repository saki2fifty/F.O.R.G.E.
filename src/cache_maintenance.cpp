#include "cache_maintenance.hpp"
#include "import_job_cleanup.hpp"
#include <forge/derived_cache.hpp>
namespace forge {
nlohmann::json maintain_asset_cache(const ProjectLease& lease, CacheMaintenance operation,
                                    AssetId asset, std::uint64_t budget, std::stop_token stop) {
    using Json = nlohmann::json;
    lease.check();
    if (stop.stop_requested())
        throw std::runtime_error("Cache maintenance cancelled");
    const auto catalog = AssetCatalog::open_project(lease.root());
    std::set<std::string> selected;
    std::map<std::string, std::vector<AssetId>> owners;
    for (const auto& [id, record] : catalog.records()) {
        const auto imported = record.metadata.find("forge.import");
        if (imported == record.metadata.end())
            continue;
        const auto key = imported->at("key").get<std::string>();
        if (!valid_content_digest(key))
            throw std::runtime_error("Catalog contains invalid selected cache identity");
        selected.insert(key);
        owners[key].push_back(id);
    }
    DerivedDataCache cache(lease.root(), {256 * 1024 * 1024, 512 * 1024 * 1024, 4096});
    Json result{{"ok", true}, {"selected_artifacts", selected.size()}};
    if (operation == CacheMaintenance::Verify) {
        result["entries"] = cache.verify_storage(stop);
        for (const auto& entry : result["entries"])
            if (!entry.at("integrity_ok").get<bool>())
                result["ok"] = false;
        result["format_validation"] = "Runtime/importer format admission remains separate";
    } else if (operation == CacheMaintenance::PruneUnused) {
        result["removed_bytes"] = cache.prune(budget, selected);
        result["budget_bytes"] = budget;
    } else if (operation == CacheMaintenance::ClearAsset) {
        const auto found = catalog.records().find(asset);
        if (found == catalog.records().end() || !found->second.metadata.contains("forge.import"))
            throw std::runtime_error(
                "Select a registered imported asset before clearing its cache");
        const auto key = found->second.metadata.at("forge.import").at("key").get<std::string>();
        result["affected_assets"] = owners.at(key);
        result["removed_bytes"] = cache.erase({key});
    } else if (operation == CacheMaintenance::ClearAll) {
        result["affected_assets"] = Json::array();
        for (const auto& [key, ids] : owners) {
            (void)key;
            for (const auto id : ids)
                result["affected_assets"].push_back(id);
        }
        result["removed_bytes"] = cache.erase(cache.keys());
        result["cleanup"] = cache.cleanup_orphans(true);
    } else if (operation == CacheMaintenance::Cleanup) {
        result["cleanup"] = cache.cleanup_orphans();
    } else if (operation != CacheMaintenance::Statistics) {
        throw std::runtime_error("Unsupported cache maintenance operation");
    }
    if (result.contains("cleanup"))
        for (const auto& entry : result["cleanup"])
            if (!entry.at("removed").get<bool>())
                result["ok"] = false;
    if (operation == CacheMaintenance::Cleanup || operation == CacheMaintenance::ClearAll) {
        result["worker_jobs"] = asset_detail::cleanup_import_jobs(lease, stop);
        for (const auto& entry : result["worker_jobs"])
            if (!entry.at("removed").get<bool>())
                result["ok"] = false;
    }
    const auto stats = cache.statistics();
    result["statistics"] = {
        {"entries", stats.entries}, {"bytes", stats.bytes}, {"quarantined", stats.quarantined}};
    return result;
}
} // namespace forge
