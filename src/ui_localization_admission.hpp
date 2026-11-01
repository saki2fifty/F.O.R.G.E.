#pragma once
#include "asset_bytes.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/ui_localization.hpp>

namespace forge {
// The same typed boundary serves candidate UI admission and runtime packaging.
inline UiLocalizationData admit_ui_localization_artifact(const AssetRecord& record,
                                                         const CachedArtifact& artifact) {
    const auto& selection = record.metadata.at("forge.import");
    const auto& metadata = record.metadata.at("forge.ui_localization");
    const auto& input = artifact.manifest.at("inputs");
    if (record.type != UiLocalizationAsset::type || record.schema_version != 1 || record.subasset ||
        selection.at("version") != 1 || selection.at("importer") != "forge.ui.localization" ||
        selection.at("importer_revision") != "forge.ui.localization.v1" ||
        selection.at("output_format") != "forge.ui-localization" ||
        selection.at("output_version") != 1 || !selection.at("generation").is_number_unsigned() ||
        !selection.at("generation").get<std::uint64_t>() || artifact.key != selection.at("key") ||
        artifact.files.size() != 1 || artifact.files.front().name != "localization.json")
        throw std::runtime_error("Unsupported UI localization artifact selection");
    for (const auto* field : {"importer", "importer_revision", "output_format", "output_version",
                              "platform", "backend", "profile", "settings_version"})
        if (input.at(field) != selection.at(field))
            throw std::runtime_error("UI localization recipe mismatch");
    if (input.at("settings_version") != 1 || !input.at("settings").is_object() ||
        !input.at("settings").empty() || !input.at("dependencies").is_array() ||
        !input.at("dependencies").empty() || !input.at("source_dependencies").is_object() ||
        !input.at("source_dependencies").empty() ||
        input.at("source") != selection.at("source_digest") ||
        asset_build_digest(input.at("settings")) != selection.at("settings_digest") ||
        asset_build_digest(nlohmann::json{{"assets", input.at("dependencies")},
                                          {"sources", input.at("source_dependencies")}}) !=
            selection.at("dependency_digest") ||
        asset_build_digest(artifact.manifest.at("files")) != selection.at("artifact_digest"))
        throw std::runtime_error("UI localization input/provenance mismatch");
    const auto& file = artifact.files.front();
    if (metadata.at("version") != 1 || metadata.at("file") != file.name ||
        metadata.at("bytes") != file.bytes.size() ||
        metadata.at("sha256") != asset_detail::content_digest(file.bytes))
        throw std::runtime_error("UI localization artifact metadata mismatch");
    const auto source = UiLocalizationSource::parse(file.bytes);
    if (source.asset() != record.id)
        throw std::runtime_error("Cooked UI localization identity mismatch");
    return source.compiled();
}
} // namespace forge
