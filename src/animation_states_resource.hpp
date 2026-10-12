#pragma once
#include "asset_bytes.hpp"
#include <forge/animation_states.hpp>
#include <forge/assets.hpp>
#include <forge/derived_cache.hpp>
#include <forge/resource.hpp>
namespace forge {
template <> struct ResourceTraits<AnimationStatesAsset> {
    using Data = AnimationStates;
};
inline ResourceTicket request_animation_states(ResourcePool<AnimationStatesAsset>& pool,
                                               const std::filesystem::path& project,
                                               const AssetRecord& record) {
    if (record.type != AnimationStatesAsset::type || record.subasset)
        throw std::runtime_error("Animation States asset is missing or has the wrong type");
    const auto selection = record.metadata.at("forge.import");
    const auto metadata = record.metadata.at("forge.animation_states");
    const auto revision = selection.at("key").get<std::string>();
    const auto generation = selection.at("generation").get<std::uint64_t>();
    if (selection.at("version") != 1 || selection.at("output_format") != "forge.animation-states" ||
        (selection.at("output_version") != 1 && selection.at("output_version") != 2) || !generation)
        throw std::runtime_error("Unsupported Animation States selection");
    return pool.request(
        AssetRef<AnimationStatesAsset>{record.id}, revision, generation,
        [project, id = record.id, selection, metadata, revision](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Animation States load cancelled");
            DerivedDataCache cache(project, {animation_states_cooked_byte_limit,
                                             animation_states_cooked_byte_limit + 65536, 1});
            std::optional<AnimationStates> value;
            const auto artifact = cache.load_selected(revision, [&](const auto& cooked) {
                if (cooked.files.size() != 1 || cooked.files.front().name != "states.json")
                    throw std::runtime_error("Invalid Animation States artifact");
                std::uint32_t version;
                const auto source =
                    AnimationStatesSource::parse_cooked(cooked.files.front().bytes, version);
                if (version != selection.at("output_version"))
                    throw std::runtime_error("Animation States cooked version/selection mismatch");
                if (source.asset() != id)
                    throw std::runtime_error("Cooked Animation States identity mismatch");
                value = source.compiled();
            });
            const auto& input = artifact.manifest.at("inputs");
            for (const auto* field : {"importer", "importer_revision", "output_format",
                                      "output_version", "platform", "backend", "profile"})
                if (input.at(field) != selection.at(field))
                    throw std::runtime_error("Animation States recipe mismatch");
            const auto& file = artifact.files.front();
            if (!value || input.at("source") != selection.at("source_digest") ||
                selection.at("artifact_digest") !=
                    asset_build_digest(artifact.manifest.at("files")) ||
                metadata.at("version") != 1 || metadata.at("file") != file.name ||
                metadata.at("sha256") != asset_detail::content_digest(file.bytes) ||
                metadata.at("bytes") != file.bytes.size() || stop.stop_requested())
                throw std::runtime_error("Animation States artifact/provenance mismatch");
            auto result = std::make_unique<AnimationStates>(std::move(*value));
            const auto bytes = result->resident_bytes();
            return ResourceCandidate<AnimationStatesAsset>{std::move(result), {0, 0, 0, 0, bytes}};
        });
}
} // namespace forge
