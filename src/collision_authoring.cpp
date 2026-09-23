// clang-format off
#include <Jolt/Jolt.h>
// clang-format on
#include "collision_authoring.hpp"
#include "asset_bytes.hpp"
#include "collision_bundle.hpp"
#include "collision_shape.hpp"
#include "model_render_resource.hpp"
#include <forge/engine_assets.hpp>
namespace forge {
namespace {
void require(bool v, const char* why) {
    if (!v)
        throw std::runtime_error(why);
}
void cancelled(std::stop_token stop) {
    require(!stop.stop_requested(), "Collision import cancelled");
}
AssetImporterDescriptor descriptor() {
    AssetImporterDescriptor d;
    d.id = "forge.collision.builtin";
    d.revision = FORGE_COLLISION_RECIPE_FINGERPRINT;
    d.label = "Collision";
    d.description = "Prepare explicit collision recipes from selected immutable Mesh revisions.";
    d.extensions = {".json"};
    d.source_kinds = {"forge.collision"};
    d.output_types = {CollisionAsset::type};
    d.output_format = "forge.collision-bundle";
    d.targets = {{"windows", "none", "cpu"}, {"linux", "none", "cpu"}};
    d.execution = ImportExecution::TrustedCpuTask;
    d.limits.output_files = 2;
    d.limits.output_bytes = 65 * 1024 * 1024;
    return d;
}
AssetDependency dependency(const AssetCatalog& catalog, AssetRef<MeshAsset> ref) {
    if (const auto* builtin = engine_asset(ref.id)) {
        require(std::string_view(builtin->type) == MeshAsset::type,
                "Collision source is not a Mesh");
        return {ref.id, MeshAsset::type, AssetDependencyKind::Build, "collision.mesh",
                engine_asset_revision(ref.id)};
    }
    const auto found = catalog.records().find(ref.id);
    require(found != catalog.records().end() && found->second.type == MeshAsset::type &&
                (!found->second.subasset || !found->second.subasset->removed),
            "Collision Mesh source is missing, removed or has the wrong type");
    const auto& imported = found->second.metadata.at("forge.import");
    require(imported.at("version") == 1 &&
                valid_content_digest(imported.at("key").get<std::string>()),
            "Collision Mesh source has no usable cooked revision");
    return {ref.id, MeshAsset::type, AssetDependencyKind::Build, "collision.mesh",
            imported.at("key").get<std::string>()};
}
struct Captured {
    CollisionSource source;
    std::shared_ptr<const AssetCatalog> catalog;
    AssetBuildInput input;
};
Captured capture(const AssetImportRequest& request, std::stop_token stop) {
    cancelled(stop);
    const auto bytes = asset_detail::read_bytes(
        ProjectPaths(request.project).resolve(request.source), 1024 * 1024);
    Captured out{CollisionSource::parse(bytes),
                 std::make_shared<const AssetCatalog>(AssetCatalog::open_project(request.project)),
                 {}};
    require(out.source.asset() == request.asset, "Collision source/catalog AssetId mismatch");
    auto& input = out.input;
    input.source_digest = asset_detail::content_digest(bytes);
    input.importer = "forge.collision.builtin";
    input.importer_revision = FORGE_COLLISION_RECIPE_FINGERPRINT;
    input.output_format = "forge.collision-bundle";
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    for (const auto& ref : out.source.mesh_sources())
        input.dependencies.push_back(dependency(*out.catalog, ref));
    for (const auto& node : out.source.document.at("nodes"))
        if (node.contains("source") && node.at("source").at("parts") != "all") {
            const auto& selected = node.at("source");
            const auto edge =
                dependency(*out.catalog, selected.at("mesh").get<AssetRef<MeshAsset>>());
            require(selected.at("revision") == edge.revision,
                    "Collision source part selection is stale: review the changed Mesh and "
                    "reselect its parts");
        }
    auto graph = out.catalog->dependency_graph();
    graph.replace(request.asset, input.dependencies);
    (void)graph.build_order(std::array{request.asset});
    return out;
}
class CollisionImporter final : public AssetImporter {
  public:
    CollisionImporter()
        : AssetImporter(::forge::descriptor(), {"forge.collision.builtin", 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& probe) const override {
        return path_utf8(probe.source).ends_with(".collision.json")
                   ? ImportProbeResult{ImportProbeMatch::Possible, "forge.collision",
                                       "Collision source document suffix"}
                   : ImportProbeResult{};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported collision import target");
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
                "Collision source/dependency changed after discovery");
        ResourcePool<MeshAsset> meshes({1, 1, 16, 512ull * 1024 * 1024});
        auto resolved = resolve_collision_source(snapshot.source, [&](AssetRef<MeshAsset> mesh) {
            cancelled(stop);
            auto ticket =
                asset_detail::request_model_mesh(meshes, request.project, snapshot.catalog, mesh);
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (!meshes.wait(ticket, std::chrono::milliseconds(20))) {
                cancelled(stop);
                const auto info = ticket.inspect();
                if (resource_detail::terminal(info.state))
                    throw std::runtime_error("Collision Mesh preparation: " + info.diagnostic);
                require(std::chrono::steady_clock::now() < end,
                        "Collision Mesh preparation timed out");
            }
            auto data = meshes.acquire(ticket)->mesh;
            meshes.unload(mesh);
            meshes.collect();
            return data;
        });
        cancelled(stop);
        if (progress)
            progress(.8, "Validating collision geometry");
        return collision_detail::encode_bundle(request.asset, resolved);
    }
    void validate(const CachedArtifact& artifact) const override {
        const auto decoded = collision_detail::decode_bundle(artifact.files);
        (void)physics_detail::prepare_collision(decoded.geometry);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> collision_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<CollisionImporter>());
    registry->seal();
    return registry;
}
void prepare_collision_publication(AssetPublicationCandidate& candidate,
                                   const AssetImportPlan& plan) {
    require(plan.input.importer == "forge.collision.builtin" &&
                plan.data.at("asset").get<AssetId>() == candidate.ticket.owner &&
                candidate.sidecar.identity.entries.empty(),
            "Collision publication identity/family mismatch");
    const auto decoded = collision_detail::decode_bundle(candidate.files, candidate.ticket.owner);
    auto& identity = candidate.sidecar.identity;
    identity.owner = candidate.ticket.owner;
    identity.source = candidate.ticket.source;
    identity.source_digest = candidate.input.source_digest;
    identity.evidence_schema = "forge.collision.single.v1";
    candidate.records = {{candidate.ticket.owner,
                          CollisionAsset::type,
                          candidate.ticket.source,
                          1,
                          {},
                          {{"forge.collision",
                            {{"version", 1},
                             {"static_only", collision_contains_triangle_mesh(decoded.geometry)},
                             {"jolt_revision", collision_detail::jolt_revision}}}}}};
}
} // namespace forge
