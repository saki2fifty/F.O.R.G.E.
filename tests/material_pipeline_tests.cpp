#include "material_authoring.hpp"
#include "material_document.hpp"
#include "material_selection.hpp"
#include "material_watch_tests.hpp"
#include "model_render_resource.hpp"
#include "runtime_resource_tests.hpp"
#include "texture_bundle_validation.hpp"
#include <forge/material_source.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
void save(const std::filesystem::path& path, const nlohmann::json& document) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << document.dump();
    require(bool(out.flush()), "Fixture source save failed");
}
AssetImportOutcome finish(AssetImportService& service) {
    require(service.wait_idle(10s), "Material import stalled");
    auto completed = service.poll();
    require(completed.size() == 1, "Material import receipt missing");
    return std::move(completed.front());
}
void submit(AssetImportService& service, const char* path) {
    service.submit(
        service.prepare(path),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_material_publication(candidate, plan);
        },
        [](const auto&, const auto& artifact) { (void)decode_material_bundle(artifact.files); });
}
AssetId texture_fixture(const std::filesystem::path& root, AssetCatalog& catalog,
                        TextureDimension dimension) {
    TextureData texture;
    texture.width = texture.height = 1;
    texture.dimension = dimension;
    texture.format = TextureFormat::RGBA8Srgb;
    texture.semantic = TextureSemantic::Color;
    texture.subresources.resize(dimension == TextureDimension::Cube ? 6 : 1,
                                std::vector<std::byte>(4, std::byte{255}));
    const auto bytes = encode_texture(texture);
    TextureBundleIndex index;
    index.variants = {{texture.semantic, texture_variant_file(texture.semantic),
                       content_digest(bytes), bytes.size()}};
    AssetBuildInput input;
    input.source_digest = content_digest(bytes);
    input.importer = "forge.texture.fixture";
    input.importer_revision = std::string(64, 'a');
    input.output_format = "forge.texture-bundle";
    input.platform = "linux";
    input.backend = "none";
    input.profile = "cpu";
    DerivedDataCache cache(root);
    auto artifact = cache.publish(
        input,
        {{"texture.json", encode_texture_bundle_index(index)}, {index.variants[0].file, bytes}},
        [](const auto& a) { (void)validate_texture_bundle(a.files); });
    const auto id = AssetId::generate();
    catalog.add({id,
                 "texture",
                 "Assets/" + id.str() + ".texture",
                 1,
                 {},
                 {{"forge.import",
                   {{"version", 1},
                    {"generation", std::uint64_t(1)},
                    {"key", input.key()},
                    {"source_digest", input.source_digest},
                    {"importer", input.importer},
                    {"importer_revision", input.importer_revision},
                    {"output_format", input.output_format},
                    {"output_version", 1},
                    {"artifact_digest", asset_build_digest(artifact.manifest.at("files"))}}}}});
    return id;
}
} // namespace
#include "surface_material_pipeline_tests.hpp"
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need material pipeline scratch directory");
        forge::test::runtime_resource_services(std::filesystem::absolute(argv[1]));
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        auto base = MaterialSource::create(AssetId::generate());
        auto instance = MaterialSource::create(AssetId::generate());
        instance.document["base"] = base.asset();
        base.document["overrides"]["parameters"]["roughnessFactor"] = {
            {"type", unsigned(MaterialParameterType::Scalar)}, {"value", {.2f}}};
        save(root / "Assets/base.material.json", base.document);
        save(root / "Assets/instance.material.json", instance.document);
        AssetCatalog catalog(root);
        catalog.add({base.asset(), "material", "Assets/base.material.json"});
        catalog.add({instance.asset(), "material", "Assets/instance.material.json"});
        catalog.save(AssetCatalog::project_index(root));
        AssetImportService service(lease, material_import_registry(), {"linux", "none", "cpu"});
        submit(service, "Assets/base.material.json");
        auto done = finish(service);
        require(done.published, "Base material failed: " + done.diagnostic);
        submit(service, "Assets/instance.material.json");
        done = finish(service);
        require(done.published, "Instance material failed: " + done.diagnostic);
        catalog = AssetCatalog::open_project(root);
        auto first = load_material_selection(root, catalog, {instance.asset()});
        require(first.data.values.parameters.at("roughnessFactor").value[0] == .2f,
                "Instance failed to inherit published value");
        require(catalog.dependency_graph().invalidated_by(base.asset()) ==
                    std::vector{instance.asset()},
                "Base publication cannot invalidate dependent instance");
        // The renderer's existing model/engine entry point must consume root materials.
        ResourcePool<MaterialAsset> pool;
        const auto ticket = request_model_pbr_material(
            pool, root, std::make_shared<const AssetCatalog>(catalog), {instance.asset()});
        require(pool.wait(ticket, 5s), "Material resource stalled");
        auto resource = pool.acquire(ticket);
        require(bool(resource), "Root material did not enter shared resource pool");
        auto mismatched_catalog = catalog;
        auto corrupt = mismatched_catalog.records().at(instance.asset());
        corrupt.metadata["forge.import"]["generation"] = std::uint64_t(999);
        corrupt.metadata["forge.material"]["textures"]["unexpected"] = AssetId::generate();
        mismatched_catalog.replace(std::move(corrupt));
        const auto bad_ticket = request_model_pbr_material(
            pool, root, std::make_shared<const AssetCatalog>(mismatched_catalog),
            {instance.asset()});
        require(!pool.wait(bad_ticket, 5s) &&
                    pool.current({instance.asset()}, "builtin:gltf-pbr-v1").identity() ==
                        resource.identity(),
                "Mismatched bindings replaced last-good live material");
        submit(service, "Assets/instance.material.json");
        done = finish(service);
        require(done.published && done.cache_hit,
                "Repeated material import did not use validated cache");
        base.document["overrides"]["parameters"]["roughnessFactor"]["value"] = {.8f};
        save(root / "Assets/base.material.json", base.document);
        submit(service, "Assets/base.material.json");
        done = finish(service);
        require(done.published, "Base update failed: " + done.diagnostic);
        // Instance remains an immutable last-good revision until its own publication.
        catalog = AssetCatalog::open_project(root);
        require(load_material_selection(root, catalog, {instance.asset()}).revision ==
                    first.revision,
                "Base update silently replaced instance revision");
        submit(service, "Assets/instance.material.json");
        done = finish(service);
        require(done.published, "Dependent rebuild failed: " + done.diagnostic);
        catalog = AssetCatalog::open_project(root);
        const auto updated = load_material_selection(root, catalog, {instance.asset()});
        require(updated.revision != first.revision &&
                    updated.data.values.parameters.at("roughnessFactor").value[0] == .8f,
                "Rebuilt instance ignored new base revision");
        instance.document["overrides"]["parameters"]["roughnessFactor"] = {
            {"type", unsigned(MaterialParameterType::Scalar)}, {"value", {-1}}};
        save(root / "Assets/instance.material.json", instance.document);
        const auto last_good = catalog.document();
        submit(service, "Assets/instance.material.json");
        done = finish(service);
        require(!done.published && !done.diagnostic.empty(), "Invalid material published");
        require(AssetCatalog::open_project(root).document() == last_good,
                "Invalid material changed the catalog");
        // A rejected source edit cannot poison an existing compiled base chain.
        auto child = MaterialSource::create(AssetId::generate());
        child.document["base"] = instance.asset();
        save(root / "Assets/child.material.json", child.document);
        catalog.add({child.asset(), "material", "Assets/child.material.json"});
        catalog.save(AssetCatalog::project_index(root));
        submit(service, "Assets/child.material.json");
        done = finish(service);
        require(done.published, "Last-good inherited material unusable: " + done.diagnostic);
        // Indirect cycle through an already published instance must reject.
        base.document["base"] = child.asset();
        save(root / "Assets/base.material.json", base.document);
        const auto before_cycle = AssetCatalog::open_project(root).document();
        submit(service, "Assets/base.material.json");
        done = finish(service);
        require(!done.published && done.diagnostic.find("cycle") != std::string::npos,
                "Indirect material inheritance cycle accepted: " + done.diagnostic);
        require(AssetCatalog::open_project(root).document() == before_cycle,
                "Cycle rejection changed usable catalog");
        // A finished candidate still rechecks source bytes before publication.
        base.document.erase("base");
        save(root / "Assets/base.material.json", base.document);
        submit(service, "Assets/base.material.json");
        require(service.wait_idle(10s), "Stale source import stalled");
        base.document["external"] = true;
        save(root / "Assets/base.material.json", base.document);
        auto stale = service.poll();
        require(stale.size() == 1 && !stale.front().published &&
                    AssetCatalog::open_project(root).document() == before_cycle,
                "Stale source changed usable catalog");
        const auto job = service.submit(
            service.prepare("Assets/base.material.json"),
            [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); },
            [](const auto&, const auto&) {});
        service.cancel(job);
        done = finish(service);
        require(!done.published && AssetCatalog::open_project(root).document() == before_cycle,
                "Cancelled material changed usable catalog");
        auto fresh = MaterialSource::create(AssetId::generate());
        fresh.document["plugin.extension"] = {{"opaque", {1, 2, 3}}};
        save(root / "Assets/fresh.material.json", fresh.document);
        MaterialDocument document(lease, "Assets/fresh.material.json");
        const auto initial_revision = document.revision();
        document.edit(initial_revision, "Change roughness", [](auto& j) {
            j["overrides"]["parameters"]["roughnessFactor"] = {
                {"type", unsigned(MaterialParameterType::Scalar)}, {"value", {.3f}}};
        });
        require(document.dirty() && document.can_undo() && document.revision() > initial_revision,
                "Material draft did not own source history");
        document.undo();
        require(!document.dirty() && document.can_redo(),
                "Material Undo did not restore clean source");
        document.redo();
        document.save();
        require(!document.dirty() && document.source().document.at("plugin.extension") ==
                                         fresh.document.at("plugin.extension"),
                "Material save lost opaque data/dirty boundary");
        // First publication must use the source UUID; no provisional identity replacement.
        auto fresh_draft = service.prepare("Assets/fresh.material.json", {}, fresh.asset());
        require(fresh_draft.ticket.owner == fresh.asset(),
                "Authored first publication changed UUID");
        service.submit(
            fresh_draft,
            [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); },
            [](const auto&, const auto& a) { (void)decode_material_bundle(a.files); });
        done = finish(service);
        require(done.published, "Authored first material publication failed: " + done.diagnostic);
        bool rejected = false;
        try {
            service.prepare("Assets/fresh.material.json", {}, AssetId::generate());
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Authored import replaced existing UUID");
        document.edit(document.revision(), "Set two-sided",
                      [](auto& j) { j["overrides"]["double_sided"] = true; });
        const auto before_stale_edit = document.source().document;
        rejected = false;
        try {
            document.edit(initial_revision, "Stale edit",
                          [](auto& j) { j["overrides"]["alpha"] = 2; });
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && document.source().document == before_stale_edit,
                "Stale material edit changed source");
        auto external = document.source().document;
        external["external-change"] = true;
        save(root / "Assets/fresh.material.json", external);
        rejected = false;
        try {
            document.save();
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && document.dirty() && document.source().document == before_stale_edit,
                "Conflicting material save overwrote external data/draft");
        MaterialDocument reopened(lease, "Assets/fresh.material.json");
        require(reopened.source().document == external, "External material bytes were overwritten");
        catalog = AssetCatalog::open_project(root);
        const auto color = texture_fixture(root, catalog, TextureDimension::D2);
        const auto cube = texture_fixture(root, catalog, TextureDimension::Cube);
        catalog.save(AssetCatalog::project_index(root));
        auto textured = external;
        MaterialData slot_values;
        slot_values.model = "forge.gltf.metallic-roughness.v1";
        slot_values.textures["normalTexture"].semantic = TextureSemantic::Normal;
        textured["overrides"]["textures"]["normalTexture"] = {
            {"asset", color},
            {"slot", material_values_document(slot_values).at("textures").at("normalTexture")}};
        save(root / "Assets/fresh.material.json", textured);
        const auto before_bad_texture = catalog.document();
        submit(service, "Assets/fresh.material.json");
        done = finish(service);
        require(!done.published &&
                    AssetCatalog::open_project(root).document() == before_bad_texture,
                "Missing Normal semantic variant replaced last-good material");
        textured["overrides"]["textures"].clear();
        slot_values.textures.clear();
        slot_values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        textured["overrides"]["textures"]["baseColorTexture"] = {
            {"asset", cube},
            {"slot", material_values_document(slot_values).at("textures").at("baseColorTexture")}};
        save(root / "Assets/fresh.material.json", textured);
        submit(service, "Assets/fresh.material.json");
        done = finish(service);
        require(!done.published &&
                    AssetCatalog::open_project(root).document() == before_bad_texture,
                "Cube texture replaced valid material requiring a 2D slot");
        textured["overrides"]["textures"]["baseColorTexture"]["asset"] = color;
        save(root / "Assets/fresh.material.json", textured);
        submit(service, "Assets/fresh.material.json");
        done = finish(service);
        require(done.published, "Compatible texture selection failed: " + done.diagnostic);
        test_material_watch(root / "watch-project");
        test_surface_material_pipeline(root / "surface-project");
        std::cout << "Material source/publication/resource/last-good/ancestry checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
