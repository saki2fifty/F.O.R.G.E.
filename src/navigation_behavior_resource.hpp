#pragma once
#include "navigation_behavior_admission.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/navigation_behavior.hpp>
#include <forge/resource.hpp>

namespace forge {
template <> struct ResourceTraits<NavigationBehaviorAsset> {
    using Data = NavigationBehaviorGraph;
};
// Existing bounded resource pool owns preparation/adoption/leases and last-good.
inline ResourceTicket request_navigation_behavior(ResourcePool<NavigationBehaviorAsset>& pool,
                                                  const std::filesystem::path& project,
                                                  const AssetRecord& record) {
    if (record.type != NavigationBehaviorAsset::type || record.schema_version != 1 ||
        record.subasset)
        throw std::runtime_error("Navigation behavior asset has the wrong type/version");
    const auto selection = record.metadata.at("forge.import");
    const auto revision = selection.at("key").get<std::string>();
    const auto generation = selection.at("generation").get<std::uint64_t>();
    if (selection.at("version") != 1 ||
        selection.at("output_format") != "forge.navigation-behavior" ||
        selection.at("output_version") != 1 || !generation)
        throw std::runtime_error("Unsupported navigation behavior selection");
    return pool.request(AssetRef<NavigationBehaviorAsset>{record.id}, revision, generation,
                        [project, record, revision](std::stop_token stop) {
                            if (stop.stop_requested())
                                throw std::runtime_error("Navigation behavior load cancelled");
                            DerivedDataCache cache(
                                project, {navigation_behavior_cooked_byte_limit,
                                          navigation_behavior_cooked_byte_limit + 65536, 1});
                            const auto artifact = cache.load_selected(revision, [](const auto&) {});
                            auto result = std::make_unique<NavigationBehaviorGraph>(
                                admit_navigation_behavior_artifact(record, artifact));
                            if (stop.stop_requested())
                                throw std::runtime_error("Navigation behavior load cancelled");
                            const auto bytes = result->resident_bytes();
                            return ResourceCandidate<NavigationBehaviorAsset>{std::move(result),
                                                                              {0, 0, 0, 0, bytes}};
                        });
}
} // namespace forge
