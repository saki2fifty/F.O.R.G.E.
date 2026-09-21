#include "material_authoring.hpp"
#include "asset_bytes.hpp"
#include "material_selection.hpp"
#include "model_render_resource.hpp"
#include <algorithm>
#include <forge/material_source.hpp>
#include <forge/texture_bundle.hpp>
namespace forge {
namespace {
using namespace asset_detail;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void cancelled(std::stop_token stop) {
    require(!stop.stop_requested(), "Material import cancelled");
}
AssetImporterDescriptor descriptor() {
    AssetImporterDescriptor d;
    d.id = "forge.material.builtin";
    d.revision = FORGE_MATERIAL_RECIPE_FINGERPRINT;
    d.label = "Built-in material";
    d.description =
        "Resolve explicit material overrides against selected immutable asset revisions.";
    d.extensions = {".json"};
    d.source_kinds = {"forge.material"};
    d.output_types = {"material"};
    d.output_format = "forge.material-bundle";
    d.targets = {{"windows", "d3d12", "desktop"}, {"linux", "none", "cpu"}};
    d.execution = ImportExecution::TrustedCpuTask;
    d.limits.output_files = 2;
    d.limits.output_bytes = 3 * 1024 * 1024;
    return d;
}
AssetDependency dependency(const AssetCatalog& catalog, AssetId id, const char* type,
                           AssetDependencyKind kind, std::string role) {
    const auto found = catalog.records().find(id);
    require(found != catalog.records().end() && found->second.type == type &&
                (!found->second.subasset || !found->second.subasset->removed),
            "Material dependency is missing, removed or has the wrong type");
    const auto& imported = found->second.metadata.at("forge.import");
    require(imported.at("version") == 1 &&
                valid_content_digest(imported.at("key").get<std::string>()),
            "Material dependency has no usable published revision");
    return {id, type, kind, std::move(role), imported.at("key").get<std::string>()};
}
struct Snapshot {
    AssetBuildInput input;
    MaterialResourceData material;
};
void validate_texture_selections(const AssetImportRequest& request,
                                 const std::shared_ptr<const AssetCatalog>& catalog,
                                 const MaterialResourceData& material, std::stop_token stop) {
    if (material.textures.empty())
        return;
    // Reuse cooked resource admission, one selection at a time. Do not decode
    // sources or accumulate the complete material's image payloads in this job.
    ResourcePool<TextureAsset> pool({1, 1, 64, 512ull * 1024 * 1024});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (const auto& [role, ref] : material.textures) {
        cancelled(stop);
        const auto& slot = material.values.textures.at(role);
        const auto ticket = request_texture(pool, request.project, catalog, ref, slot.semantic);
        while (!pool.wait(ticket, std::chrono::milliseconds(20))) {
            cancelled(stop);
            const auto info = ticket.inspect();
            if (resource_detail::terminal(info.state))
                throw std::runtime_error("Material texture " + role + ": " + info.diagnostic);
            require(std::chrono::steady_clock::now() < deadline,
                    "Material texture admission timed out");
        }
        {
            const auto texture = pool.acquire(ticket);
            require(texture && texture->dimension == slot.dimension &&
                        texture->semantic == slot.semantic,
                    "Material texture dimension/semantic is incompatible with its slot");
        }
        pool.unload(ref, std::string(texture_variant_key(slot.semantic)));
        pool.collect();
    }
}
Snapshot capture(const AssetImportRequest& request, std::stop_token stop) {
    cancelled(stop);
    const auto bytes = read_bytes(ProjectPaths(request.project).resolve(request.source),
                                  material_source_byte_limit);
    const auto source = MaterialSource::parse(bytes);
    require(source.asset() == request.asset,
            "Material source AssetId differs from its catalog identity");
    auto catalog = AssetCatalog::open_project(request.project);
    Snapshot result;
    auto& input = result.input;
    input.source_digest = content_digest(bytes);
    input.importer = "forge.material.builtin";
    input.importer_revision = FORGE_MATERIAL_RECIPE_FINGERPRINT;
    input.settings = nlohmann::json::object();
    input.output_format = "forge.material-bundle";
    input.platform = request.target.platform;
    input.backend = request.target.backend;
    input.profile = request.target.profile;
    std::optional<ResolvedMaterialSource> base;
    if (const auto ref = source.base()) {
        input.dependencies.push_back(dependency(catalog, ref->id, MaterialAsset::type,
                                                AssetDependencyKind::Build, "material.base"));
        auto data = load_pbr_material(request.project, catalog, *ref, stop);
        base = ResolvedMaterialSource{std::move(data.values), std::move(data.textures)};
    }
    const auto resolved = resolve_material_source(source, base ? &*base : nullptr);
    result.material = {resolved.values, resolved.textures};
    for (const auto& [role, ref] : resolved.textures)
        input.dependencies.push_back(dependency(catalog, ref.id, TextureAsset::type,
                                                AssetDependencyKind::Runtime,
                                                "material.texture:" + role));
    // Use the catalog's shared graph, including indirect material/model ancestry.
    // This is a candidate copy; rejection never mutates the selected catalog.
    auto graph = catalog.dependency_graph();
    graph.replace(request.asset, input.dependencies);
    const std::array roots{request.asset};
    (void)graph.build_order(roots);
    validate_texture_selections(request, std::make_shared<const AssetCatalog>(std::move(catalog)),
                                result.material, stop);
    cancelled(stop);
    return result;
}
class MaterialImporter final : public AssetImporter {
  public:
    MaterialImporter() : AssetImporter(::forge::descriptor(), {"forge.material.builtin", 1, {}}) {}
    ImportProbeResult probe(const ImportProbe& p) const override {
        if (path_utf8(p.source).ends_with(".material.json"))
            return {ImportProbeMatch::Possible, "forge.material",
                    "Material source document suffix"};
        return {};
    }
    AssetImportPlan discover(const AssetImportRequest& request,
                             std::stop_token stop) const override {
        require(std::find(descriptor().targets.begin(), descriptor().targets.end(),
                          request.target) != descriptor().targets.end(),
                "Unsupported built-in material target");
        (void)settings().effective(request.settings);
        auto snapshot = capture(request, stop);
        AssetImportPlan plan;
        plan.input = std::move(snapshot.input);
        plan.data = {{"asset", request.asset}};
        return plan;
    }
    std::vector<ArtifactFile>
    import_and_cook(const AssetImportRequest& request, const AssetImportPlan& plan,
                    std::stop_token stop,
                    const std::function<void(double, std::string)>& progress) const override {
        const auto snapshot = capture(request, stop);
        require(snapshot.input.document() == plan.input.document(),
                "Material source/base selection changed after discovery");
        if (progress)
            progress(.8, "Validating resolved material");
        return encode_material_bundle(request.asset, snapshot.material);
    }
    void validate(const CachedArtifact& artifact) const override {
        (void)decode_material_bundle(artifact.files);
    }
};
} // namespace
std::shared_ptr<const AssetImporterRegistry> material_import_registry() {
    auto registry = std::make_shared<AssetImporterRegistry>();
    registry->add(std::make_shared<MaterialImporter>());
    registry->seal();
    return registry;
}
void prepare_material_publication(AssetPublicationCandidate& c, const AssetImportPlan& plan) {
    require(plan.input.importer == "forge.material.builtin" &&
                plan.data.at("asset").get<AssetId>() == c.ticket.owner &&
                c.sidecar.identity.entries.empty(),
            "Material publication identity/family mismatch");
    const auto data = decode_material_bundle(c.files, c.ticket.owner);
    auto& identity = c.sidecar.identity;
    identity.owner = c.ticket.owner;
    identity.source = c.ticket.source;
    identity.source_digest = c.input.source_digest;
    identity.evidence_schema = "forge.material.single.v1";
    c.records = {{c.ticket.owner,
                  "material",
                  c.ticket.source,
                  1,
                  {},
                  {{"forge.material", {{"version", 1}, {"textures", data.textures}}}}}};
}
} // namespace forge
