#include "asset_file_operations.hpp"
#include "asset_file_transaction.hpp"
#include "asset_reference_impact.hpp"
#include "asset_reimport.hpp"
#include "model_render_resource.hpp"
#include "root_mesh_artifact.hpp"
#include "runtime_package.hpp"
#include "terrain_authoring.hpp"
#include <forge/asset_discovery.hpp>
#include <iostream>
#include <thread>
using namespace forge;
using namespace forge::asset_detail;
using namespace std::chrono_literals;
namespace {
void require(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected invalid terrain operation rejection");
}
AssetImportOutcome publish(AssetImportService& service, const std::filesystem::path& source,
                           AssetId id) {
    service.submit(
        service.prepare(source, {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_terrain_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    require(service.wait_idle(10s), "Terrain import timed out");
    auto results = service.poll();
    require(results.size() == 1, "Terrain import receipt missing");
    return std::move(results[0]);
}
MeshData load(const std::filesystem::path& root, std::shared_ptr<const AssetCatalog> catalog,
              AssetId id) {
    ResourcePool<MeshAsset> pool;
    const auto ticket = request_model_mesh(pool, root, std::move(catalog), {id});
    require(pool.wait(ticket, 10s), "Terrain Mesh load failed: " + ticket.inspect().diagnostic);
    const auto lease = pool.acquire(ticket);
    require(bool(lease), "Terrain mesh lease missing");
    require(lease->materials.size() == 1 && lease->materials[0].key == "default",
            "Terrain lost stable default material binding");
    return lease->mesh;
}
void watch(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "Assets");
    auto owner = std::make_shared<ProjectLease>(root);
    const std::filesystem::path path = "Assets/Watched.terrain.json";
    auto document = TerrainDocument::create(
        owner, path, [](AssetId id) { return TerrainSource::create(id, 5, 4); });
    const auto id = document->source().asset();
    auto source = document->source();
    auto registry = terrain_import_registry();
    AssetImportService importer(owner, registry, {"linux", "none", "cpu"});
    require(publish(importer, path, id).published, "Terrain watch initial publication failed");
    SourceScanOptions options;
    options.include_project_root = true;
    AssetReimportService automatic(owner,
                                   {{terrain_importer,
                                     {"linux", "none", "cpu"},
                                     registry,
                                     [](auto& candidate, const auto& plan, const auto&) {
                                         prepare_terrain_publication(candidate, plan);
                                     }}},
                                   options, 10min, 20ms);
    std::vector<AssetImportOutcome> receipts;
    auto poll = [&] {
        for (auto& result : automatic.poll())
            receipts.push_back(std::move(result));
    };
    auto finish = [&] {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        do {
            poll();
            require(std::chrono::steady_clock::now() < deadline,
                    "Terrain automatic update timed out");
            std::this_thread::sleep_for(1ms);
        } while (!automatic.complete() || !automatic.settled() || automatic.scanning());
    };
    finish();
    require(receipts.empty(), "Unchanged terrain rebuilt at source-watch startup");
    const auto before = AssetCatalog::open_project(root).document();
    source.brush(source.raster().layers[0].id, std::array<MeshBrushSample, 1>{{{{2, 0, 2}, 1}}}, 2,
                 TerrainBrush::Raise, 2);
    atomic_write(root / path, source.document.dump(2));
    bool blocked = true;
    automatic.blocked = [&](AssetId changed) { return blocked && changed == id; };
    automatic.rescan();
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (automatic.queued() != 1) {
        poll();
        require(std::chrono::steady_clock::now() < deadline, "Changed terrain did not queue");
        std::this_thread::sleep_for(1ms);
    }
    require(receipts.empty() && automatic.jobs().empty() &&
                AssetCatalog::open_project(root).document() == before,
            "Blocked terrain draft was overwritten by automatic source updates");
    blocked = false;
    finish();
    require(receipts.size() == 1 && receipts.front().published,
            "Unblocked terrain source change did not publish exactly once");
    auto current = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    require(encode_mesh(load(root, current, id)) == encode_mesh(source.mesh()),
            "Automatic terrain update selected different geometry");
    receipts.clear();
    automatic.rescan();
    finish();
    require(receipts.empty(), "Terrain publisher self-writes caused a reimport loop");
    const auto good = current->document();
    source.document["tiles"][0]["layers"][0]["mask"][0] = 2;
    atomic_write(root / path, source.document.dump());
    automatic.rescan();
    finish();
    require(!receipts.empty() && !receipts.back().published &&
                !receipts.back().diagnostic.empty() &&
                AssetCatalog::open_project(root).document() == good,
            "Invalid watched terrain replaced the working selected Mesh");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need terrain asset scratch path");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        auto document = TerrainDocument::create(lease, "Assets/Field.terrain.json", [](AssetId id) {
            return TerrainSource::create(id, 5, 4);
        });
        const auto initial = document->source().document;
        const auto id = document->source().asset();
        auto operation = [](Json& draft) {
            TerrainSource source{draft};
            const auto layer = source.raster().layers[0].id;
            source.add_tile({1, 0});
            source.add_layer("Detail");
            source.brush(layer, std::array<MeshBrushSample, 2>{{{{3, 0, 2}, 1}, {{4, 0, 2}, .5}}},
                         2, TerrainBrush::Raise, 2);
            source.document["extensions"] = {{"author", {{"opaque", 9}}}};
            draft = std::move(source.document);
        };
        document->edit(document->revision(), "Terrain tiles and recorded stroke", operation);
        const auto edited = document->source().document;
        const auto revision = document->revision();
        reject([&] { document->edit(revision - 1, "Stale", operation); });
        reject([&] {
            document->edit(revision, "Root identity",
                           [](auto& value) { value["asset_id"] = AssetId::generate(); });
        });
        reject([&] {
            document->edit(revision, "Torn seam",
                           [](auto& value) { value["tiles"][1]["layers"][0]["heights"][0] = 1; });
        });
        require(document->revision() == revision && document->source().document == edited,
                "Invalid edit changed terrain history");
        document->undo();
        require(document->source().document == initial && !document->dirty(),
                "Terrain stroke/tile Undo differs");
        document->redo();
        require(document->source().document == edited, "Terrain redo lost layered source");
        document->undo();
        document->edit(document->revision(), "New layer branch", [](auto& draft) {
            TerrainSource source{draft};
            source.add_layer("Branch");
            draft = source.document;
        });
        require(!document->can_redo() &&
                    document->source().document["layers"][1]["id"] != edited["layers"][1]["id"],
                "Terrain branch reused abandoned layer identity");
        document->undo();
        document->edit(document->revision(), "Final layered terrain", operation);
        document->save();
        const auto saved = document->source().document;
        const auto expected = encode_mesh(document->source().mesh());
        TerrainDocument reopened(lease, document->locator());
        require(reopened.source().document == saved,
                "Terrain saved source differs after reopening");
        AssetImportService service(lease, terrain_import_registry(), {"linux", "none", "cpu"});
        auto first = publish(service, document->locator(), id);
        require(first.published && !first.cache_hit,
                "Terrain first cook failed: " + first.diagnostic);
        auto catalog = std::make_shared<const AssetCatalog>(first.publication->catalog);
        require(encode_mesh(load(root, catalog, id)) == expected,
                "Published terrain geometry differs from source projection");
        auto second = publish(service, document->locator(), id);
        require(second.published && second.cache_hit,
                "Terrain did not reuse unchanged immutable cook");
        catalog = std::make_shared<const AssetCatalog>(second.publication->catalog);
        const auto record = catalog->records().at(id);
        const auto key = selected_mesh_revision(*catalog, {id});
        const auto on_disk = read_bytes(root / document->locator(), terrain_source_byte_limit);
        auto invalid = saved;
        invalid["tiles"][1]["layers"][0]["mask"][0] = 0;
        atomic_write(root / document->locator(), invalid.dump());
        auto failed = publish(service, document->locator(), id);
        require(!failed.published && !failed.diagnostic.empty() &&
                    AssetCatalog::open_project(root).document() == catalog->document(),
                "Failed Terrain cook replaced last good selection");
        require(encode_mesh(load(root, catalog, id)) == expected &&
                    selected_mesh_revision(*catalog, {id}) == key,
                "Last good terrain resource changed after failed source edit");
        atomic_write(root / document->locator(),
                     std::string(reinterpret_cast<const char*>(on_disk.data()), on_disk.size()));
        const auto sources = scan_asset_sources(root);
        require(sources.complete && sources.files.at(document->locator()).source_kind == "terrain",
                "Content discovery did not recognize terrain source");
        const std::array drafts{AssetReferenceDocument{document->locator(), saved}};
        const auto references = collect_asset_references({{"components", Json::array()}}, drafts);
        require(references.references.empty() && references.uninspected.size() == 1,
                "Terrain reference scan confused local IDs with assets or lost opaque extension "
                "warning");
        auto copy = prepare_asset_file_operation(
            root, {AssetFileAction::Duplicate, id, "Assets/Copy.terrain.json"},
            project_asset_file_rewriter(root));
        const auto copy_id = copy.result;
        require(copy_id != id, "Terrain duplicate reused root identity");
        AssetFileTransaction transaction(*lease);
        (void)transaction.commit(std::move(copy.changes), false);
        TerrainDocument copied(lease, "Assets/Copy.terrain.json");
        require(copied.source().asset() == copy_id &&
                    copied.source().document.at("tiles") == saved.at("tiles") &&
                    copied.source().document.at("layers") == saved.at("layers") &&
                    copied.source().document.at("extensions") == saved.at("extensions"),
                "Terrain duplicate changed root-scoped tile/layer identities or extensions");
        const auto unbuilt_copy = AssetCatalog::open_project(root).records().at(copy_id);
        require(!unbuilt_copy.metadata.contains("forge.import") &&
                    !unbuilt_copy.metadata.contains("forge.mesh"),
                "Terrain copy inherited a cooked revision claim");
        require(publish(service, "Assets/Copy.terrain.json", copy_id).published,
                "Terrain duplicate failed its own cook");
        reject([&] {
            rewrite_authored_asset(record, saved.dump(), "Assets/Wrong.mesh.json",
                                   {{id, AssetId::generate()}});
        });
        auto altered = record;
        auto tampered = *catalog;
        for (const auto field : {"profile", "hash", "recipe", "bytes", "generation"}) {
            altered = record;
            if (std::string_view(field) == "profile")
                altered.metadata["forge.terrain"]["profile"] = "future";
            else if (std::string_view(field) == "hash")
                altered.metadata["forge.mesh"]["sha256"] = std::string(64, '0');
            else if (std::string_view(field) == "bytes")
                altered.metadata["forge.mesh"]["bytes"] = 1;
            else if (std::string_view(field) == "generation")
                altered.metadata["forge.import"]["generation"] = 0u;
            else
                altered.metadata["forge.import"]["importer_revision"] = "forge.mesh.editable.v1";
            tampered.replace(altered);
            reject([&] { load(root, std::make_shared<const AssetCatalog>(tampered), id); });
        }
        auto malformed = document->source().mesh();
        malformed.lods[0].parts[0].indices[0] = 1;
        reject([&] { validate_heightfield_mesh(malformed); });
        const auto output = root / "Export";
        const std::array roots{id};
        (void)package_runtime_content(root, output, roots, {"linux", "none"});
        const auto relocated = root.parent_path() / (AssetId::generate().str() + "-relocated");
        std::filesystem::rename(output, relocated);
        const auto packaged = std::make_shared<const AssetCatalog>(
            open_runtime_content(relocated, {"linux", "none"}));
        require(encode_mesh(load(relocated, packaged, id)) == expected &&
                    !std::filesystem::exists(relocated / document->locator()),
                "Relocated source-free terrain export lost geometry or shipped source");
        std::filesystem::remove(root / document->locator());
        require(encode_mesh(load(root, catalog, id)) == expected,
                "Terrain resource reread offline source");
        watch(root / "Watch");
        std::cout << "Terrain history, cook, strict admission, copy, last-good and relocated "
                     "source-free package passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
