#pragma once
#include <forge/assets.hpp>
#include <forge/project_lease.hpp>
#include <stop_token>
namespace forge {
enum class CacheMaintenance { Statistics, Verify, PruneUnused, ClearAsset, ClearAll, Cleanup };
// Caller owns the project writer and drains import/publication jobs first.
// This operation changes disposable cache only; never authored metadata/history.
nlohmann::json maintain_asset_cache(const ProjectLease&, CacheMaintenance, AssetId asset = {},
                                    std::uint64_t budget_bytes = 0, std::stop_token stop = {});
} // namespace forge
