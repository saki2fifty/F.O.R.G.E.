#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/animation_mask.hpp>
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/resource.hpp>
#include <map>
namespace forge {
struct AnimationMaskResource {
    AnimationMask mask;
    std::string skeleton_revision;
    std::size_t resident_bytes() const {
        return sizeof(*this) + mask.resident_bytes() - sizeof(mask) + skeleton_revision.capacity();
    }
};
template <> struct ResourceTraits<AnimationMaskAsset> {
    using Data = AnimationMaskResource;
};
inline ResourceTicket request_animation_mask(ResourcePool<AnimationMaskAsset>& pool,
                                             const std::filesystem::path& project,
                                             const AssetRecord& record) {
    if (record.type != AnimationMaskAsset::type || record.subasset)
        throw std::runtime_error("Animation Mask asset is missing or has the wrong type");
    const auto selection = record.metadata.at("forge.import");
    const auto metadata = record.metadata.at("forge.animation_mask");
    const auto revision = selection.at("key").get<std::string>();
    const auto generation = selection.at("generation").get<std::uint64_t>();
    if (selection.at("version") != 1 || selection.at("importer") != "forge.animation.mask" ||
        selection.at("importer_revision") != "forge.animation.mask.v1" ||
        selection.at("output_format") != "forge.animation-mask" ||
        selection.at("output_version") != 1 || !generation)
        throw std::runtime_error("Unsupported Animation Mask selection");
    auto edges = record.dependency_edges;
    std::sort(edges.begin(), edges.end());
    return pool.request(
        AssetRef<AnimationMaskAsset>{record.id}, revision, generation,
        [project, id = record.id, selection, metadata, revision, edges](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Animation Mask load cancelled");
            DerivedDataCache cache(project, {animation_mask_cooked_byte_limit,
                                             animation_mask_cooked_byte_limit + 65536, 1});
            std::optional<AnimationMask> value;
            const auto artifact = cache.load_selected(revision, [&](const auto& cooked) {
                if (cooked.files.size() != 1 || cooked.files.front().name != "mask.json")
                    throw std::runtime_error("Invalid Animation Mask artifact");
                value = decode_animation_mask(cooked.files.front().bytes, id);
            });
            const auto& input = artifact.manifest.at("inputs");
            for (const auto* field : {"importer", "importer_revision", "output_format",
                                      "output_version", "platform", "backend", "profile"})
                if (input.at(field) != selection.at(field))
                    throw std::runtime_error("Animation Mask recipe mismatch");
            const auto& file = artifact.files.front();
            auto cooked_edges = input.at("dependencies").get<std::vector<AssetDependency>>();
            std::sort(cooked_edges.begin(), cooked_edges.end());
            if (!value || input.at("source") != selection.at("source_digest") ||
                cooked_edges != edges ||
                selection.at("artifact_digest") !=
                    asset_build_digest(artifact.manifest.at("files")) ||
                metadata.at("version") != 1 || metadata.at("file") != file.name ||
                metadata.at("sha256") != asset_detail::content_digest(file.bytes) ||
                metadata.at("bytes") != file.bytes.size())
                throw std::runtime_error("Animation Mask artifact/provenance mismatch");
            std::map<AssetId, AssetDependency> expected;
            for (const auto& edge : edges) {
                if (edge.kind != AssetDependencyKind::Runtime || edge.revision.empty() ||
                    !expected.emplace(edge.target, edge).second)
                    throw std::runtime_error("Animation Mask dependency contract is invalid");
            }
            if (expected.size() != value->entries.size() + 1 || !expected.contains(value->skeleton))
                throw std::runtime_error("Animation Mask dependency closure is incomplete");
            const auto& skeleton = expected.at(value->skeleton);
            if (skeleton.expected_type != SkeletonAsset::type || skeleton.role != "mask.skeleton")
                throw std::runtime_error("Animation Mask skeleton dependency is invalid");
            for (const auto& entry : value->entries) {
                if (!expected.contains(entry.joint))
                    throw std::runtime_error("Animation Mask joint dependency is missing");
                const auto& joint = expected.at(entry.joint);
                if (joint.expected_type != ModelNodeAsset::type ||
                    joint.role != "mask.joint:" + entry.joint.str() ||
                    joint.revision != skeleton.revision)
                    throw std::runtime_error(
                        "Animation Mask joint dependency mixes model revisions");
            }
            if (stop.stop_requested())
                throw std::runtime_error("Animation Mask load cancelled");
            auto result = std::make_unique<AnimationMaskResource>(
                AnimationMaskResource{std::move(*value), skeleton.revision});
            const auto bytes = result->resident_bytes();
            return ResourceCandidate<AnimationMaskAsset>{std::move(result), {0, 0, 0, 0, bytes}};
        });
}
} // namespace forge
