#include "particle_effect_authoring.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "model_draw_candidate.hpp"
#include "particle_render_instances.hpp"
#include <algorithm>
#include <forge/engine_assets.hpp>
#include <thread>
namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Snapshot {
    ParticleEffectSource source;
    AssetBuildInput input;
};
void admit_draw_profile(const AssetImportRequest& request, const AssetCatalog& catalog,
                        const ParticleEffectData& values, std::stop_token stop) {
    auto selected = std::make_shared<const AssetCatalog>(catalog);
    ResourcePool<MeshAsset> meshes({1, 1, 1, 64ull * 1024 * 1024});
    ResourcePool<MaterialAsset> materials({1, 16, 16, 64ull * 1024 * 1024});
    ResourcePool<TextureAsset> textures({1, 32, 32, 128ull * 1024 * 1024});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto ticket =
        asset_detail::request_model_mesh(meshes, request.project, selected, values.mesh);
    while (!meshes.wait(ticket, std::chrono::milliseconds(10))) {
        require(!stop.stop_requested(), "Particle draw admission cancelled");
        const auto state = ticket.inspect();
        require(!resource_detail::terminal(state.state), "Particle mesh could not be admitted");
        require(std::chrono::steady_clock::now() < deadline, "Particle mesh admission timed out");
    }
    const auto mesh = meshes.acquire(ticket);
    require(bool(mesh), "Particle mesh lease is unavailable");
    validate_particle_mesh_profile(mesh.get());
    std::vector<MaterialSlotOverride> overrides;
    if (values.material.id)
        overrides.push_back({mesh->materials.front().key, values.material});
    asset_detail::ModelDrawCandidate candidate(request.project, selected, 1, values.mesh,
                                               std::move(overrides), meshes);
    while (!candidate.ready()) {
        require(!stop.stop_requested(), "Particle draw admission cancelled");
        meshes.pump();
        materials.pump();
        textures.pump();
        candidate.advance(1, meshes, materials, textures);
        if (candidate.state() != ResourceState::Loading && !candidate.ready())
            throw std::runtime_error("Particle draw admission failed: " + candidate.diagnostic());
        require(std::chrono::steady_clock::now() < deadline, "Particle draw admission timed out");
        if (!candidate.ready())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!candidate.ready()->has_fallbacks() && candidate.ready()->selection.unresolved.empty(),
            "Particle draw profile needs complete mesh/material bindings");
    for (const auto& [id, material] : candidate.ready()->materials)
        validate_particle_material_profile(material.get());
}
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Particle effect import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), particle_effect_byte_limit);
    Snapshot snapshot{ParticleEffectSource::parse(bytes), {}};
    require(snapshot.source.asset() == request.asset,
            "Particle effect source/catalog identity mismatch");
    const auto values = snapshot.source.compiled();
    const auto catalog = AssetCatalog::open_project(request.project);
    auto& input = snapshot.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = particle_effect_importer;
    input.importer_revision = particle_effect_recipe;
    input.output_format = particle_effect_cooked_format;
    input.output_version = 2;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    auto dependency = [&](AssetId id, const char* type, const char* role) {
        std::string revision;
        if (const auto* builtin = engine_asset(id)) {
            require(std::string_view(builtin->type) == type,
                    "Particle engine dependency has wrong type");
            revision = engine_asset_revision(id);
        } else {
            const auto selected = catalog.resolve(id, type);
            require(selected.state == AssetState::Available,
                    "Particle dependency is missing, removed or has wrong type");
            const auto& imported = selected.record->metadata.at("forge.import");
            require(imported.at("version") == 1, "Particle dependency has unsupported selection");
            revision = imported.at("key").get<std::string>();
        }
        require(valid_content_digest(revision), "Particle dependency has no immutable revision");
        input.dependencies.push_back(
            {id, type, AssetDependencyKind::Runtime, role, std::move(revision)});
    };
    dependency(values.mesh.id, MeshAsset::type, "particles.mesh");
    if (values.material.id)
        dependency(values.material.id, MaterialAsset::type, "particles.material");
    auto graph = catalog.dependency_graph();
    graph.replace(request.asset, input.dependencies);
    const std::array roots{request.asset};
    (void)graph.build_order(roots);
    admit_draw_profile(request, catalog, values, stop);
    require(!stop.stop_requested(), "Particle effect import cancelled");
    return snapshot;
}
const ArtifactFile& effect_file(const std::vector<ArtifactFile>& files, AssetId id) {
    require(files.size() == 1 && files.front().name == "effect.json",
            "Particle effect artifact needs one effect.json");
    (void)decode_cooked_particle_effect(files.front().bytes, id);
    return files.front();
}
class ParticleEffectImporter final : public AssetImporter {
  public:
    ParticleEffectImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = particle_effect_importer;
                  d.revision = particle_effect_recipe;
                  d.label = "Particle Effect";
                  d.description = "Cook a reusable bounded CPU mesh-particle effect.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.particle_effect"};
                  d.output_types = {ParticleEffectAsset::type};
                  d.output_format = particle_effect_cooked_format;
                  d.output_version = 2;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.limits.output_files = 1;
                  d.limits.output_bytes = particle_effect_byte_limit;
                  return d;
              }(),
              {particle_effect_importer, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(".particles.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.particle_effect",
                                       "Particle Effect source suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported particle effect target");
        (void)settings().effective(request.settings);
        auto snapshot = capture(request, stop);
        return {std::move(snapshot.input), {}, {{"asset", request.asset}}};
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        auto snapshot = capture(request, stop);
        require(snapshot.input.document() == plan.input.document(),
                "Particle source or dependency changed after discovery");
        if (progress)
            progress(.5, "Validating emission, lifetime and local motion");
        auto cooked = snapshot.source.cook();
        require(!stop.stop_requested(), "Particle effect import cancelled");
        return {{"effect.json", std::move(cooked)}};
    }
    void validate(const CachedArtifact& artifact) const override {
        require(artifact.files.size() == 1, "Invalid particle effect artifact files");
        const auto source = asset_detail::parse_bounded_json(artifact.files.front().bytes,
                                                             particle_effect_byte_limit);
        (void)effect_file(artifact.files, source.at("asset_id").get<AssetId>());
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> particle_effect_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<ParticleEffectImporter>());
    registry->seal();
    return registry;
}
void prepare_particle_effect_publication(AssetPublicationCandidate& candidate,
                                         const AssetImportPlan& plan) {
    require(plan.input.importer == particle_effect_importer &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Particle effect publication identity/family mismatch");
    const auto& file = effect_file(candidate.files, candidate.ticket.owner);
    require(plan.input.importer_revision == particle_effect_recipe &&
                plan.input.output_format == particle_effect_cooked_format &&
                plan.input.output_version == 2 &&
                decode_cooked_particle_effect(file.bytes, candidate.ticket.owner).version == 2,
            "Particle publication recipe/value version mismatch");
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.particle-effect.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          ParticleEffectAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.particle_effect",
                            {{"version", 2},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
