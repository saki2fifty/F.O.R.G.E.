#pragma once
#include "asset_bytes.hpp"
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <string_view>

namespace forge::asset_detail {
struct SingleSourceArtifactProfile {
    std::string_view type, importer, revision, format, file, metadata;
};
// Shared selected-artifact provenance. Format codecs retain semantic admission.
inline const ArtifactFile& admit_single_source_artifact(const AssetRecord& record,
                                                        const CachedArtifact& artifact,
                                                        const SingleSourceArtifactProfile& p) {
    const auto& selection = record.metadata.at("forge.import");
    const auto& metadata = record.metadata.at(std::string(p.metadata));
    const auto& input = artifact.manifest.at("inputs");
    if (record.type != p.type || record.schema_version != 1 || record.subasset ||
        selection.at("version") != 1 || selection.at("importer") != p.importer ||
        selection.at("importer_revision") != p.revision ||
        selection.at("output_format") != p.format || selection.at("output_version") != 1 ||
        !selection.at("generation").is_number_unsigned() ||
        !selection.at("generation").get<std::uint64_t>() || artifact.key != selection.at("key") ||
        artifact.files.size() != 1 || artifact.files.front().name != p.file)
        throw std::runtime_error("Unsupported selected single-source artifact");
    for (const auto* field : {"importer", "importer_revision", "output_format", "output_version",
                              "platform", "backend", "profile", "settings_version"})
        if (input.at(field) != selection.at(field))
            throw std::runtime_error("Selected single-source recipe mismatch");
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
        throw std::runtime_error("Selected single-source input/provenance mismatch");
    const auto& file = artifact.files.front();
    if (metadata.at("version") != 1 || metadata.at("file") != file.name ||
        metadata.at("bytes") != file.bytes.size() ||
        metadata.at("sha256") != content_digest(file.bytes))
        throw std::runtime_error("Selected single-source artifact metadata mismatch");
    return file;
}
} // namespace forge::asset_detail
