#include "model_selection.hpp"
#include "asset_bytes.hpp"
#include <forge/model_asset.hpp>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void check_file(const Json& metadata, const ModelMemberFile& file) {
    require(metadata.at("version") == 1 && metadata.at("file") == file.file &&
                metadata.at("sha256") == file.digest && metadata.at("bytes") == file.bytes,
            "Selected model member metadata differs from immutable artifact");
}
} // namespace
const ModelImportMember& ModelSelection::member(AssetId id) const {
    const auto found = member_indices.find(id);
    require(found != member_indices.end(), "Asset is not an active member of this model revision");
    return index.members.at(found->second);
}
std::span<const std::byte> ModelSelection::bytes(const ModelImportMember& member) const {
    require(!member.node && !member.material_variant,
            "Inline model members have no standalone artifact bytes");
    const auto found = file_indices.find(member.artifact.file);
    require(artifact && found != file_indices.end(), "Selected model member file is missing");
    return artifact->files.at(found->second).bytes;
}
ModelSelection load_model_selection(const std::filesystem::path& project,
                                    const AssetCatalog& catalog, AssetId model,
                                    std::stop_token stop) {
    auto cancelled = [&] { require(!stop.stop_requested(), "Model resource load cancelled"); };
    cancelled();
    const auto owner = catalog.records().find(model);
    require(owner != catalog.records().end() && owner->second.type == ModelAsset::type &&
                !owner->second.subasset,
            "Model resource owner is unavailable or has wrong type");
    const auto& record = owner->second;
    const auto& selected = record.metadata.at("forge.import");
    require(selected.at("version") == 1 && selected.at("output_format") == "forge.model-bundle" &&
                (selected.at("output_version") == 1 || selected.at("output_version") == 2 ||
                 selected.at("output_version") == 3 || selected.at("output_version") == 4),
            "Unsupported selected model artifact profile");
    ModelSelection result;
    result.owner = model;
    result.revision = selected.at("key").get<std::string>();
    const auto& generation = selected.at("generation");
    require(generation.is_number_unsigned() && generation.get<std::uint64_t>() > 0,
            "Invalid selected model publication generation");
    result.generation = generation.get<std::uint64_t>();
    DerivedDataCache cache(project, {256 * 1024 * 1024, 512 * 1024 * 1024, 4096});
    auto artifact = cache.load_selected(result.revision, [&](const auto& candidate) {
        cancelled();
        result.index = validate_model_bundle(candidate.files);
    });
    require(selected.at("output_version") == result.index.version,
            "Selected model format version differs from immutable artifact");
    require(selected.at("artifact_digest") == asset_build_digest(artifact.manifest.at("files")) &&
                selected.at("source_digest") == result.index.source_digest,
            "Selected model catalog and cooked revision disagree");
    const auto& inputs = artifact.manifest.at("inputs");
    require(inputs.at("source") == result.index.source_digest &&
                inputs.at("importer") == selected.at("importer") &&
                inputs.at("importer_revision") == selected.at("importer_revision") &&
                inputs.at("output_format") == selected.at("output_format") &&
                inputs.at("output_version") == selected.at("output_version"),
            "Model artifact recipe differs from selected catalog revision");
    if (result.index.hierarchy.contains("animation"))
        require(inputs.at("tool_revisions").at("gltf2ozz") ==
                    result.index.hierarchy.at("animation").at("provenance").at("converter_sha256"),
                "Model converter provenance differs from immutable build input");
    bool found_index = false;
    for (const auto& file : artifact.files)
        if (file.name == "model.json") {
            check_file(record.metadata.at("forge.model"),
                       {file.name, content_digest(file.bytes), file.bytes.size()});
            found_index = true;
        }
    require(found_index, "Selected model index is missing");
    std::set<AssetId> unique;
    for (const auto& edge : record.dependency_edges) {
        require(edge.kind == AssetDependencyKind::Runtime &&
                    edge.role.starts_with("model.member:") && edge.revision == result.revision &&
                    unique.insert(edge.target).second &&
                    result.bindings.emplace(edge.role.substr(13), edge.target).second,
                "Invalid selected model member edge");
    }
    require(result.bindings.size() == result.index.members.size(),
            "Selected model member count differs from immutable artifact");
    for (std::size_t i = 0; i < result.index.members.size(); ++i) {
        cancelled();
        const auto& member = result.index.members[i];
        const auto id = result.bindings.at(member.identity.address);
        require(result.member_indices.emplace(id, i).second,
                "Selected model member identity is duplicated");
        const auto found = catalog.records().find(id);
        require(found != catalog.records().end(), "Selected model member identity is missing");
        const auto& child = found->second;
        require(child.type == member.identity.type && child.subasset &&
                    child.subasset->owner == model && !child.subasset->removed &&
                    child.source == record.source,
                "Selected model member ownership or type differs");
        const auto& imported = child.metadata.at("forge.import");
        require(imported == selected, "Selected model family mixes publication revisions");
        if (member.material_variant) {
            const auto& metadata = child.metadata.at("forge.model");
            require(metadata.at("version") == 3 &&
                        metadata.at("material_variant") == *member.material_variant &&
                        !metadata.contains("file") && !metadata.contains("sha256") &&
                        !metadata.contains("bytes"),
                    "Selected inline material variant differs from immutable hierarchy");
        } else if (member.node) {
            const auto& metadata = child.metadata.at("forge.model");
            require(metadata.at("version") == 2 && metadata.at("node") == *member.node &&
                        !metadata.contains("file") && !metadata.contains("sha256") &&
                        !metadata.contains("bytes"),
                    "Selected inline model node differs from immutable hierarchy");
        } else
            check_file(child.metadata.at("forge.model"), member.artifact);
        require(child.dependency_edges.size() == member.bindings.size(),
                "Selected model member binding count differs");
        for (const auto& edge : child.dependency_edges) {
            const auto target = member.bindings.find(edge.role);
            require(target != member.bindings.end() && edge.kind == AssetDependencyKind::Runtime &&
                        edge.target == result.bindings.at(target->second) &&
                        edge.revision == result.revision &&
                        catalog.records().contains(edge.target) &&
                        catalog.records().at(edge.target).type == edge.expected_type,
                    "Selected model member typed binding differs from immutable artifact");
        }
    }
    // Validate root edge expected types separately after resolving every member.
    for (const auto& edge : record.dependency_edges)
        require(catalog.records().at(edge.target).type == edge.expected_type,
                "Model root member expected type mismatch");
    for (std::size_t i = 0; i < artifact.files.size(); ++i)
        require(result.file_indices.emplace(artifact.files[i].name, i).second,
                "Selected model artifact filename is duplicated");
    cancelled();
    result.artifact = std::make_shared<const CachedArtifact>(std::move(artifact));
    return result;
}
} // namespace forge::asset_detail
