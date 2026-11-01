#pragma once
#include "single_source_admission.hpp"
#include <forge/application_state_source.hpp>
#include <forge/resource.hpp>

namespace forge {
inline ApplicationStatesDefinition
admit_application_states_artifact(const AssetRecord& record, const CachedArtifact& artifact) {
    const auto& file = asset_detail::admit_single_source_artifact(
        record, artifact,
        {ApplicationStatesAsset::type, "forge.application.states", "forge.application.states.v1",
         "forge.application-states", "application-states.json", "forge.application_states"});
    const auto source = ApplicationStatesSource::parse(file.bytes);
    if (source.asset() != record.id)
        throw std::runtime_error("Cooked application state identity mismatch");
    return source.compiled();
}
template <> struct ResourceTraits<ApplicationStatesAsset> {
    using Data = ApplicationStatesDefinition;
};
inline ResourceTicket request_application_states(ResourcePool<ApplicationStatesAsset>& pool,
                                                 const std::filesystem::path& project,
                                                 const AssetRecord& record) {
    if (record.type != ApplicationStatesAsset::type || record.schema_version != 1 ||
        record.subasset)
        throw std::runtime_error("Application state asset has the wrong type/version");
    const auto& selection = record.metadata.at("forge.import");
    const auto revision = selection.at("key").get<std::string>();
    if (!selection.at("generation").is_number_unsigned() ||
        !selection.at("generation").get<std::uint64_t>())
        throw std::runtime_error("Application state selection has an invalid generation");
    return pool.request(AssetRef<ApplicationStatesAsset>{record.id}, revision,
                        selection.at("generation").get<std::uint64_t>(),
                        [project, record, revision](std::stop_token stop) {
                            if (stop.stop_requested())
                                throw std::runtime_error("Application state load cancelled");
                            DerivedDataCache cache(project,
                                                   {application_states_byte_limit,
                                                    application_states_byte_limit + 65536, 1});
                            const auto artifact = cache.load_selected(revision, [](const auto&) {});
                            auto result = std::make_unique<ApplicationStatesDefinition>(
                                admit_application_states_artifact(record, artifact));
                            if (stop.stop_requested())
                                throw std::runtime_error("Application state load cancelled");
                            const auto bytes = result->resident_bytes();
                            return ResourceCandidate<ApplicationStatesAsset>{std::move(result),
                                                                             {bytes, 0, 0, 0, 0}};
                        });
}
} // namespace forge
