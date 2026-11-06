#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/animation_graph.hpp>
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/resource.hpp>
#include <map>

namespace forge {
struct AnimationGraphResource {
    AnimationGraph graph;
    std::string skeleton_revision;
    std::map<AssetId, std::string> clips, masks;
    std::size_t resident_bytes() const {
        auto bytes =
            sizeof(*this) + graph.resident_bytes() - sizeof(graph) + skeleton_revision.capacity();
        for (const auto* revisions : {&clips, &masks})
            for (const auto& [id, revision] : *revisions) {
                (void)id;
                bytes += sizeof(decltype(clips)::value_type) + 4 * sizeof(void*) +
                         revision.capacity() + 1;
            }
        return bytes;
    }
};
template <> struct ResourceTraits<AnimationGraphAsset> {
    using Data = AnimationGraphResource;
};
inline const nlohmann::json& animation_graph_selection(const AssetRecord& record) {
    if (record.type != AnimationGraphAsset::type || record.subasset || !record.id)
        throw std::runtime_error("Animation Graph asset has the wrong identity, type or ownership");
    const auto& selection = record.metadata.at("forge.import");
    const auto& generation = selection.at("generation");
    if (!selection.at("version").is_number_integer() || selection.at("version") != 1 ||
        selection.at("importer") != "forge.animation.graph" ||
        selection.at("importer_revision") != "forge.animation.graph.v1" ||
        selection.at("output_format") != "forge.animation-graph" ||
        !selection.at("output_version").is_number_integer() ||
        selection.at("output_version") != 1 ||
        !(generation.is_number_unsigned() ||
          (generation.is_number_integer() && generation.get<std::int64_t>() > 0)) ||
        generation.get<std::uint64_t>() == 0 ||
        !valid_content_digest(selection.at("key").get<std::string>()))
        throw std::runtime_error("Unsupported Animation Graph selection");
    return selection;
}
// The cache owns file/manifest integrity. This owner admits the selected graph's
// format, identity, recipe, provenance and exact typed dependency roles.
inline AnimationGraphResource admit_animation_graph(const CachedArtifact& artifact,
                                                    const AssetRecord& record) {
    const auto& selection = animation_graph_selection(record);
    if (artifact.key != selection.at("key").get<std::string>() || artifact.files.size() != 1 ||
        artifact.files.front().name != "graph.json" ||
        artifact.files.front().bytes.size() > animation_graph_cooked_byte_limit)
        throw std::runtime_error("Invalid Animation Graph artifact identity or files");
    const auto& file = artifact.files.front();
    AnimationGraphResource value;
    value.graph = decode_animation_graph(file.bytes, record.id);
    const auto& input = artifact.manifest.at("inputs");
    for (const auto* field : {"importer", "importer_revision", "output_format", "output_version",
                              "platform", "backend", "profile"})
        if (input.at(field) != selection.at(field))
            throw std::runtime_error("Animation Graph selected recipe mismatch");
    const auto& metadata = record.metadata.at("forge.animation_graph");
    auto edges = record.dependency_edges;
    auto declared = input.at("dependencies").get<std::vector<AssetDependency>>();
    std::sort(edges.begin(), edges.end());
    std::sort(declared.begin(), declared.end());
    if (edges != declared || input.at("source") != selection.at("source_digest") ||
        selection.at("artifact_digest") != asset_build_digest(artifact.manifest.at("files")) ||
        !metadata.at("version").is_number_integer() || metadata.at("version") != 1 ||
        metadata.at("file") != file.name || metadata.at("bytes") != file.bytes.size() ||
        metadata.at("sha256") != asset_detail::content_digest(file.bytes))
        throw std::runtime_error("Animation Graph artifact/dependency provenance mismatch");
    std::map<std::string, std::pair<AssetId, std::string>> expected;
    expected.emplace("graph.skeleton",
                     std::pair{value.graph.skeleton, std::string(SkeletonAsset::type)});
    for (const auto& node : value.graph.nodes) {
        if (node.kind == AnimationGraphNodeKind::Clip)
            expected.emplace("graph.clip:" + node.id.str(),
                             std::pair{node.clip.id, std::string(AnimationClipAsset::type)});
        else if (node.mask.id)
            expected.emplace("graph.mask:" + node.id.str(),
                             std::pair{node.mask.id, std::string(AnimationMaskAsset::type)});
    }
    if (edges.size() != expected.size())
        throw std::runtime_error("Animation Graph dependency closure is incomplete");
    for (const auto& edge : edges) {
        const auto found = expected.find(edge.role);
        if (found == expected.end() || edge.target != found->second.first ||
            edge.expected_type != found->second.second ||
            edge.kind != AssetDependencyKind::Runtime || !valid_content_digest(edge.revision))
            throw std::runtime_error(
                "Animation Graph dependency role, type, identity or revision mismatch");
        if (edge.expected_type == SkeletonAsset::type)
            value.skeleton_revision = edge.revision;
        else {
            auto& revisions =
                edge.expected_type == AnimationClipAsset::type ? value.clips : value.masks;
            const auto [at, inserted] = revisions.emplace(edge.target, edge.revision);
            if (!inserted && at->second != edge.revision)
                throw std::runtime_error(
                    "Animation Graph selects multiple revisions of one dependency");
        }
        expected.erase(found);
    }
    return value;
}
inline ResourceTicket request_animation_graph(ResourcePool<AnimationGraphAsset>& pool,
                                              const std::filesystem::path& project,
                                              const AssetRecord& record) {
    const auto& selection = animation_graph_selection(record);
    const auto revision = selection.at("key").get<std::string>();
    return pool.request(
        AssetRef<AnimationGraphAsset>{record.id}, revision,
        selection.at("generation").get<std::uint64_t>(),
        [project, record, revision](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Animation Graph load cancelled");
            auto cache = DerivedDataCache::open_existing(
                project,
                {animation_graph_cooked_byte_limit, animation_graph_cooked_byte_limit + 65536, 1});
            std::optional<AnimationGraphResource> value;
            (void)cache.load_selected(revision, [&](const auto& artifact) {
                value = admit_animation_graph(artifact, record);
            });
            if (stop.stop_requested() || !value)
                throw std::runtime_error("Animation Graph load cancelled or empty");
            auto result = std::make_unique<AnimationGraphResource>(std::move(*value));
            const auto bytes = result->resident_bytes();
            return ResourceCandidate<AnimationGraphAsset>{std::move(result),
                                                          ResourceMemory{0, 0, 0, 0, bytes}};
        });
}
} // namespace forge
