#include "editable_mesh_authoring.hpp"
#include "asset_bytes.hpp"
#include <algorithm>

namespace forge {
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
constexpr const char* importer_id = "forge.mesh.editable";
constexpr const char* recipe = "forge.mesh.editable.v1";
struct Snapshot {
    EditableMeshSource source;
    AssetBuildInput input;
};
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    require(!stop.stop_requested(), "Editable Mesh import cancelled");
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), editable_mesh_source_byte_limit);
    Snapshot result{EditableMeshSource::parse(bytes), {}};
    require(result.source.asset() == request.asset,
            "Editable Mesh source/catalog identity mismatch");
    auto& input = result.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = importer_id;
    input.importer_revision = recipe;
    input.output_format = "forge.mesh-bundle";
    input.output_version = 1;
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    return result;
}
const ArtifactFile& mesh_file(const std::vector<ArtifactFile>& files) {
    require(files.size() == 1 && files[0].name == "mesh.bin",
            "Editable Mesh artifact must contain one cooked Mesh file");
    (void)decode_mesh(files[0].bytes);
    return files[0];
}
class EditableMeshImporter final : public AssetImporter {
  public:
    EditableMeshImporter()
        : AssetImporter(
              [] {
                  AssetImporterDescriptor d;
                  d.id = importer_id;
                  d.revision = recipe;
                  d.label = "Editable Mesh";
                  d.description =
                      "Cook a bounded authored polygon Mesh into the existing runtime Mesh format.";
                  d.extensions = {".json"};
                  d.source_kinds = {"forge.editable-mesh"};
                  d.output_types = {MeshAsset::type};
                  d.output_format = "forge.mesh-bundle";
                  d.output_version = 1;
                  d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
                  d.execution = ImportExecution::TrustedCpuTask;
                  d.limits.output_files = 1;
                  d.limits.output_bytes = 32 * 1024 * 1024;
                  return d;
              }(),
              {importer_id, 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& source) const override {
        return path_utf8(source.source).ends_with(".mesh.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.editable-mesh",
                                       "Editable Mesh source document suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported Editable Mesh target");
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
                "Editable Mesh source changed after import discovery");
        if (progress)
            progress(.5, "Triangulating editable polygons and corner UVs");
        const auto cooked = encode_mesh(snapshot.source.cook());
        require(!stop.stop_requested(), "Editable Mesh import cancelled");
        return {{"mesh.bin", cooked}};
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)mesh_file(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> editable_mesh_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<EditableMeshImporter>());
    registry->seal();
    return registry;
}
void prepare_editable_mesh_publication(AssetPublicationCandidate& candidate,
                                       const AssetImportPlan& plan) {
    require(plan.input.importer == importer_id &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Editable Mesh publication identity/family mismatch");
    const auto& file = mesh_file(candidate.files);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.editable-mesh.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          MeshAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.mesh",
                            {{"version", 1},
                             {"file", file.name},
                             {"sha256", asset_detail::content_digest(file.bytes)},
                             {"bytes", file.bytes.size()}}}}}};
}
} // namespace forge
