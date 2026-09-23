#include "collision_selection.hpp"
#include "collision_bundle.hpp"

namespace forge {
ResourceTicket request_collision(ResourcePool<CollisionAsset>& pool, std::filesystem::path project,
                                 std::shared_ptr<const AssetCatalog> catalog,
                                 AssetRef<CollisionAsset> ref) {
    if (!catalog)
        throw std::runtime_error("Collision resource requires a catalog snapshot");
    const auto at = catalog->records().find(ref.id);
    if (at == catalog->records().end() || at->second.type != CollisionAsset::type ||
        at->second.subasset)
        throw std::runtime_error("Collision asset is missing or has the wrong type");
    const auto selected = at->second.metadata.at("forge.import");
    const auto generation = selected.at("generation");
    if (selected.at("version") != 1 || selected.at("output_format") != "forge.collision-bundle" ||
        selected.at("output_version") != 1 || !generation.is_number_unsigned() || generation == 0)
        throw std::runtime_error("Unsupported collision selection profile/generation");
    const auto revision = selected.at("key").get<std::string>();
    const auto metadata = at->second.metadata.at("forge.collision");
    return pool.request(
        ref, revision, generation.get<std::uint64_t>(),
        [project = std::move(project), ref, revision, selected, metadata](std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Collision selection cancelled");
            DerivedDataCache cache(project, {64 * 1024 * 1024, 65 * 1024 * 1024, 2});
            std::optional<collision_detail::Bundle> loaded;
            auto artifact = cache.load_selected(revision, [&](const auto& a) {
                loaded = collision_detail::decode_bundle(a.files, ref.id);
            });
            const auto& inputs = artifact.manifest.at("inputs");
            if (selected.at("artifact_digest") !=
                    asset_build_digest(artifact.manifest.at("files")) ||
                inputs.at("source") != selected.at("source_digest") ||
                inputs.at("importer") != selected.at("importer") ||
                inputs.at("importer_revision") != selected.at("importer_revision") ||
                inputs.at("output_format") != selected.at("output_format") ||
                inputs.at("output_version") != selected.at("output_version") ||
                metadata.at("version") != 1 ||
                metadata.at("jolt_revision") != collision_detail::jolt_revision)
                throw std::runtime_error(
                    "Collision selected recipe/provenance differs from its artifact");
            if (!loaded ||
                metadata.at("static_only") != collision_contains_triangle_mesh(loaded->geometry))
                throw std::runtime_error(
                    "Collision selected motion classification differs from its artifact");
            auto result = prepare_collision_resource(std::move(loaded->geometry), stop);
            result.value->asset = ref.id;
            return result;
        });
}
} // namespace forge
