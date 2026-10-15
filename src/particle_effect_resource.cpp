#include "particle_effect_resource.hpp"
#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/engine_assets.hpp>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void selection(const AssetRecord& record) {
    require(record.type == ParticleEffectAsset::type && !record.subasset,
            "Particle Effect asset has wrong type or ownership");
    const auto& imported = record.metadata.at("forge.import");
    const bool legacy = imported.at("importer_revision") == particle_effect_legacy_recipe &&
                        imported.at("output_version") == 1;
    const bool extended = imported.at("importer_revision") == particle_effect_recipe &&
                          imported.at("output_version") == 2;
    require(imported.at("version") == 1 && imported.at("importer") == particle_effect_importer &&
                (legacy || extended) &&
                imported.at("output_format") == particle_effect_cooked_format &&
                imported.at("generation").get<std::uint64_t>() != 0 &&
                valid_content_digest(imported.at("key").get<std::string>()),
            "Unsupported Particle Effect selection");
}
void dependencies(const ParticleEffectData& data, const std::vector<AssetDependency>& edges) {
    require(edges.size() == (data.material.id ? 2u : 1u),
            "Particle Effect dependency closure is incomplete");
    bool mesh = false, material = false;
    for (const auto& edge : edges) {
        require(edge.kind == AssetDependencyKind::Runtime && valid_content_digest(edge.revision),
                "Particle Effect dependency has no immutable runtime selection");
        if (edge.role == "particles.mesh") {
            require(!mesh && edge.expected_type == MeshAsset::type && edge.target == data.mesh.id,
                    "Particle Effect mesh dependency differs from cooked data");
            mesh = true;
        } else if (edge.role == "particles.material") {
            require(!material && data.material.id && edge.expected_type == MaterialAsset::type &&
                        edge.target == data.material.id,
                    "Particle Effect material dependency differs from cooked data");
            material = true;
        } else
            throw std::runtime_error("Unknown Particle Effect dependency role");
    }
    require(mesh && material == bool(data.material.id),
            "Particle Effect dependency roles are incomplete");
}
} // namespace
ParticleEffectData admit_particle_effect_artifact(const CachedArtifact& artifact,
                                                  const AssetRecord& record) {
    selection(record);
    const auto& selected = record.metadata.at("forge.import");
    const auto& metadata = record.metadata.at("forge.particle_effect");
    require(artifact.key == selected.at("key").get_ref<const std::string&>() &&
                artifact.files.size() == 1 && artifact.files.front().name == "effect.json",
            "Particle Effect artifact identity/files mismatch");
    const auto& file = artifact.files.front();
    auto value = decode_cooked_particle_effect(file.bytes, record.id);
    const auto& input = artifact.manifest.at("inputs");
    for (const auto* field : {"importer", "importer_revision", "output_format", "output_version",
                              "platform", "backend", "profile"})
        require(input.at(field) == selected.at(field), "Particle Effect artifact recipe mismatch");
    auto cooked_edges = input.at("dependencies").get<std::vector<AssetDependency>>();
    auto edges = record.dependency_edges;
    std::sort(edges.begin(), edges.end());
    std::sort(cooked_edges.begin(), cooked_edges.end());
    require(
        cooked_edges == edges && input.at("source") == selected.at("source_digest") &&
            selected.at("artifact_digest") == asset_build_digest(artifact.manifest.at("files")) &&
            metadata.at("version") == value.version &&
            selected.at("output_version") == value.version && metadata.at("file") == file.name &&
            metadata.at("sha256") == asset_detail::content_digest(file.bytes) &&
            metadata.at("bytes") == file.bytes.size(),
        "Particle Effect artifact/provenance mismatch");
    dependencies(value, edges);
    return value;
}
ResourceTicket request_particle_effect(ResourcePool<ParticleEffectAsset>& pool,
                                       const std::filesystem::path& project,
                                       std::shared_ptr<const AssetCatalog> catalog,
                                       AssetRef<ParticleEffectAsset> ref) {
    require(bool(catalog), "Particle Effect needs a catalog selection");
    const auto found = catalog->records().find(ref.id);
    require(found != catalog->records().end(),
            "Particle Effect is missing from the selected catalog");
    auto record = found->second;
    selection(record);
    for (const auto& edge : record.dependency_edges) {
        std::string revision;
        if (const auto* builtin = engine_asset(edge.target)) {
            require(std::string_view(builtin->type) == edge.expected_type,
                    "Particle Effect engine dependency has wrong type");
            revision = engine_asset_revision(edge.target);
        } else {
            const auto dependency = catalog->records().find(edge.target);
            require(dependency != catalog->records().end() &&
                        dependency->second.type == edge.expected_type &&
                        (!dependency->second.subasset || !dependency->second.subasset->removed),
                    "Particle Effect dependency is unavailable");
            revision = dependency->second.metadata.at("forge.import").at("key").get<std::string>();
        }
        require(
            revision == edge.revision,
            "Particle Effect dependency changed; recook this effect against the selected revision");
    }
    const auto revision = record.metadata.at("forge.import").at("key").get<std::string>();
    const auto generation =
        record.metadata.at("forge.import").at("generation").get<std::uint64_t>();
    return pool.request(
        ref, revision, generation,
        [project, revision, record = std::move(record)](std::stop_token stop) {
            require(!stop.stop_requested(), "Particle Effect load cancelled");
            DerivedDataCache cache(
                project, {particle_effect_byte_limit, particle_effect_byte_limit + 65536, 1});
            std::optional<ParticleEffectData> value;
            (void)cache.load_selected(revision, [&](const auto& artifact) {
                value = admit_particle_effect_artifact(artifact, record);
            });
            require(!stop.stop_requested() && value.has_value(),
                    "Particle Effect load cancelled or empty");
            const auto bytes = sizeof(ParticleEffectData) +
                               value->size_curve.capacity() * sizeof(ParticleSizeStop) +
                               value->color_gradient.capacity() * sizeof(ParticleColorStop);
            return ResourceCandidate<ParticleEffectAsset>{
                std::make_unique<ParticleEffectData>(std::move(*value)), {0, 0, 0, 0, bytes}};
        });
}
} // namespace forge
