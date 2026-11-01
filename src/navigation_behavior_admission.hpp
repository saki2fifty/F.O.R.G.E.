#pragma once
#include "asset_bytes.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/navigation_behavior.hpp>

namespace forge {
// Shared typed admission for runtime leases and source-free package construction.
inline NavigationBehaviorGraph admit_navigation_behavior_artifact(const AssetRecord& record,
                                                                  const CachedArtifact& artifact) {
    const auto& selection = record.metadata.at("forge.import");
    const auto& metadata = record.metadata.at("forge.navigation_behavior");
    if (record.type != NavigationBehaviorAsset::type || record.schema_version != 1 ||
        record.subasset || selection.at("version") != 1 ||
        selection.at("importer") != "forge.navigation.behavior" ||
        selection.at("importer_revision") != "forge.navigation.behavior.v1" ||
        selection.at("output_format") != "forge.navigation-behavior" ||
        selection.at("output_version") != 1 || !selection.at("generation").get<std::uint64_t>() ||
        artifact.files.size() != 1 || artifact.files.front().name != "behavior.json")
        throw std::runtime_error("Unsupported navigation behavior artifact selection");
    const auto& input = artifact.manifest.at("inputs");
    for (const auto* field : {"importer", "importer_revision", "output_format", "output_version",
                              "platform", "backend", "profile"})
        if (input.at(field) != selection.at(field))
            throw std::runtime_error("Navigation behavior recipe mismatch");
    const auto& file = artifact.files.front();
    const auto digest = asset_detail::content_digest(file.bytes);
    if (input.at("source") != selection.at("source_digest") ||
        selection.at("artifact_digest") != asset_build_digest(artifact.manifest.at("files")) ||
        metadata.at("version") != 1 || metadata.at("file") != file.name ||
        metadata.at("sha256") != digest || metadata.at("bytes") != file.bytes.size())
        throw std::runtime_error("Navigation behavior artifact/provenance mismatch");
    auto source = NavigationBehaviorSource::parse_cooked(file.bytes);
    if (source.asset() != record.id)
        throw std::runtime_error("Cooked navigation behavior identity mismatch");
    auto graph = source.compiled();
    graph.cooked_digest = digest;
    return graph;
}
} // namespace forge
