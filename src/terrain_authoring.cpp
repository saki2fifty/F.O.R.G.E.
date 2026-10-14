#include "terrain_authoring.hpp"
#include "asset_bytes.hpp"
#include "root_mesh_artifact.hpp"
#include <algorithm>
namespace forge {
namespace {
void require(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
struct Snapshot {
    TerrainSource source;
    AssetBuildInput input;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Terrain import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), terrain_source_byte_limit);
    Snapshot result{TerrainSource::parse(bytes), {}};
    require(result.source.asset() == request.asset, "Terrain source/catalog identity mismatch");
    auto& input = result.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = terrain_importer;
    input.importer_revision = terrain_recipe;
    input.output_format = "forge.mesh-bundle";
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    return result;
}
const ArtifactFile& mesh_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files[0].name == "mesh.bin",
            "Terrain artifact must contain one Mesh file");
    asset_detail::validate_heightfield_mesh(decode_mesh(files[0].bytes));
    return files[0];
}
class TerrainImporter final : public AssetImporter {
  public:
    TerrainImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = terrain_importer;
                  d.revision = terrain_recipe;
                  d.label = "Terrain";
                  d.description =
                      "Cook bounded layered heightfield tiles into the shared runtime Mesh format.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.heightfield"};
                  d.output_types = {MeshAsset::type};
                  d.output_format = "forge.mesh-bundle";
                  d.output_version = 1;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.limits.output_files = 1;
                  d.limits.output_bytes = 32 * 1024 * 1024;
                  return d;
              }(),
              {terrain_importer, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(".terrain.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.heightfield",
                                       "Terrain source document suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported Terrain target");
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
                "Terrain source changed after discovery");
        if (progress)
            progress(.5, "Composing terrain layers and shared tile geometry");
        auto bytes = encode_mesh(snapshot.source.mesh());
        require(!stop.stop_requested(), "Terrain cook cancelled");
        return {{"mesh.bin", std::move(bytes)}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)mesh_file(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> terrain_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<TerrainImporter>());
    registry->seal();
    return registry;
}
void prepare_terrain_publication(AssetPublicationCandidate& candidate,
                                 const AssetImportPlan& plan) {
    require(plan.input.importer == terrain_importer &&
                plan.input.importer_revision == terrain_recipe &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Terrain publication identity/family mismatch");
    const auto& file = mesh_file(candidate.files);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.heightfield.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          MeshAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.mesh",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}},
                           {"forge.terrain", {{"version", 1}, {"profile", "heightfield_mesh1"}}}}}};
}
} // namespace forge
