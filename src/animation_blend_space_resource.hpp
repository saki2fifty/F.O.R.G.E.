#pragma once
#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/animation_blend_space.hpp>
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/resource.hpp>
namespace forge {
struct AnimationBlendSpaceResource {
    AnimationBlendSpace space;
    std::string skeleton_revision;
    std::size_t resident_bytes() const {
        return sizeof(*this) + space.resident_bytes() - sizeof(space) +
               skeleton_revision.capacity();
    }
};
template <> struct ResourceTraits<AnimationBlendSpaceAsset> {
    using Data = AnimationBlendSpaceResource;
};
inline ResourceTicket request_animation_blend_space(ResourcePool<AnimationBlendSpaceAsset>& pool,
                                                    const std::filesystem::path& project,
                                                    const AssetRecord& record) {
    if (record.type != AnimationBlendSpaceAsset::type || record.subasset)
        throw std::runtime_error("Animation Blend Space asset is missing or has the wrong type");
    const auto selection = record.metadata.at("forge.import");
    const auto metadata = record.metadata.at("forge.animation_blend_space");
    const auto revision = selection.at("key").get<std::string>();
    const auto generation = selection.at("generation").get<std::uint64_t>();
    const bool legacy = selection.at("importer_revision") == "forge.animation.blend-space.v1" &&
                        selection.at("output_version") == 1;
    const bool current = selection.at("importer_revision") == "forge.animation.blend-space.v2" &&
                         selection.at("output_version") == 2;
    if (selection.at("version") != 1 || selection.at("importer") != "forge.animation.blend-space" ||
        (!legacy && !current) || selection.at("output_format") != "forge.animation-blend-space" ||
        !generation)
        throw std::runtime_error("Unsupported Animation Blend Space selection");
    auto edges = record.dependency_edges;
    std::sort(edges.begin(), edges.end());
    return pool.request(
        AssetRef<AnimationBlendSpaceAsset>{record.id}, revision, generation,
        [project, id = record.id, selection, metadata, revision, edges](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Animation Blend Space load cancelled");
            DerivedDataCache cache(project, {animation_blend_space_cooked_byte_limit,
                                             animation_blend_space_cooked_byte_limit + 65536, 1});
            std::optional<AnimationBlendSpace> value;
            const auto artifact = cache.load_selected(revision, [&](const auto& cooked) {
                if (cooked.files.size() != 1 || cooked.files.front().name != "blendspace.json")
                    throw std::runtime_error("Invalid Animation Blend Space artifact");
                value = decode_animation_blend_space(
                    cooked.files.front().bytes, id, selection.at("output_version").get<unsigned>());
            });
            const auto& input = artifact.manifest.at("inputs");
            for (const auto* field : {"importer", "importer_revision", "output_format",
                                      "output_version", "platform", "backend", "profile"})
                if (input.at(field) != selection.at(field))
                    throw std::runtime_error("Animation Blend Space recipe mismatch");
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
                throw std::runtime_error("Animation Blend Space artifact/provenance mismatch");
            const auto skeleton = std::find_if(edges.begin(), edges.end(), [](const auto& edge) {
                return edge.role == "space.skeleton";
            });
            if (skeleton == edges.end() || skeleton->target != value->skeleton ||
                skeleton->expected_type != SkeletonAsset::type || skeleton->revision.empty())
                throw std::runtime_error("Animation Blend Space skeleton dependency is invalid");
            std::vector<AssetDependency> expected{{value->skeleton, SkeletonAsset::type,
                                                   AssetDependencyKind::Runtime, "space.skeleton",
                                                   skeleton->revision}};
            for (const auto& sample : value->samples)
                expected.push_back({sample.clip.id, AnimationClipAsset::type,
                                    AssetDependencyKind::Runtime, "space.clip:" + sample.id.str(),
                                    skeleton->revision});
            std::sort(expected.begin(), expected.end());
            if (expected != edges)
                throw std::runtime_error(
                    "Animation Blend Space dependency closure is incomplete or mixes revisions");
            if (stop.stop_requested())
                throw std::runtime_error("Animation Blend Space load cancelled");
            auto result = std::make_unique<AnimationBlendSpaceResource>(
                AnimationBlendSpaceResource{std::move(*value), skeleton->revision});
            const auto bytes = result->resident_bytes();
            return ResourceCandidate<AnimationBlendSpaceAsset>{std::move(result),
                                                               {0, 0, 0, 0, bytes}};
        });
}
} // namespace forge
