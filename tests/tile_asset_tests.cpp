#include "asset_bytes.hpp"
#include "asset_file_operations.hpp"
#include "asset_reference_impact.hpp"
#include "asset_reimport.hpp"
#include "asset_storage.hpp"
#include "editor/content_model.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include "runtime_package.hpp"
#include "tile_asset_authoring.hpp"
#include "tile_asset_resources.hpp"
#include "tile_asset_worker.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <forge/engine_texture_assets.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/scene.hpp>
#include <future>
#include <iostream>
#include <thread>

using namespace forge;
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
    throw std::runtime_error("Expected invalid tile asset operation rejection");
}
AssetImportOutcome publish(AssetImportService& service, const std::filesystem::path& path,
                           AssetId id, bool map) {
    service.submit(
        service.prepare(path, {}, id),
        [map](auto& candidate, const auto& plan, const auto&) {
            if (map)
                prepare_tile_map_publication(candidate, plan);
            else
                prepare_tile_set_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    require(service.wait_idle(30s), "Tile asset import timed out");
    auto results = service.poll();
    require(results.size() == 1, "Tile asset publication receipt missing");
    return std::move(results[0]);
}
AssetId material(const std::filesystem::path& root, std::shared_ptr<ProjectLease> lease) {
    const auto id = AssetId::generate();
    auto source = MaterialSource::create(id);
    source.document["overrides"]["model"] = "forge.gltf.unlit.v1";
    MaterialData values;
    values.model = "forge.gltf.unlit.v1";
    auto& slot = values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = TextureFilter::Nearest;
    source.document["overrides"]["textures"]["baseColorTexture"] = {
        {"asset", engine_texture(EngineTexture::Checker)},
        {"slot", material_values_document(values).at("textures").at("baseColorTexture")}};
    source.validate();
    atomic_write(root / "Assets/Atlas.material.json", source.document.dump(2));
    AssetImportService service(lease, material_import_registry(), {"linux", "none", "cpu"});
    service.submit(
        service.prepare("Assets/Atlas.material.json", {}, id),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_material_publication(candidate, plan);
        },
        [](const auto&, const auto& artifact) {
            (void)asset_detail::decode_material_bundle(artifact.files);
        });
    require(service.wait_idle(15s), "Fixture Material publication timed out");
    const auto results = service.poll();
    require(results.size() == 1 && results[0].published,
            "Fixture Material publication failed" +
                (results.empty() ? std::string{} : ": " + results[0].diagnostic));
    return id;
}
TileSetData load_set(const std::filesystem::path& root, std::shared_ptr<const AssetCatalog> catalog,
                     AssetId id) {
    ResourcePool<TileSetAsset> pool({1, 4, 8, 8 * 1024 * 1024});
    const auto ticket = request_tile_set(pool, root, std::move(catalog), {id});
    require(pool.wait(ticket, 10s), "Tile Set load failed: " + ticket.inspect().diagnostic);
    const auto lease = pool.acquire(ticket);
    require(bool(lease), "Tile Set lease missing");
    return lease.get();
}
TileMapData load_map(const std::filesystem::path& root, std::shared_ptr<const AssetCatalog> catalog,
                     AssetId id) {
    ResourcePool<TileMapAsset> pool({1, 4, 8, 64 * 1024 * 1024});
    const auto ticket = request_tile_map(pool, root, std::move(catalog), {id});
    require(pool.wait(ticket, 10s), "Tile Map load failed: " + ticket.inspect().diagnostic);
    const auto lease = pool.acquire(ticket);
    require(bool(lease), "Tile Map lease missing");
    return lease.get();
}
void recipe_guards(const TileSetSource& set, const TileMapSource& map) {
    const auto snapshot = [](std::string text) {
        const auto bytes = std::as_bytes(std::span(text));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    asset_detail::ImportProcessRequest set_request{{{"recipe", tile_set_importer_id},
                                                    {"revision", tile_set_recipe_revision()},
                                                    {"asset_id", set.asset()},
                                                    {"source_digest", ""}},
                                                   {{"source.bin", snapshot(set.document.dump())}}};
    set_request.payload["source_digest"] =
        asset_detail::content_digest(set_request.inputs[0].bytes);
    const auto definitions = asset_detail::execute_tile_recipe(set_request);
    require(definitions.size() == 1 &&
                decode_cooked_tile_set(definitions[0].bytes, set.asset()).tiles().size() == 4,
            "Direct fixed Tile Set recipe did not preserve definitions");
    asset_detail::ImportProcessRequest map_request{
        {{"recipe", tile_map_importer_id},
         {"revision", tile_map_recipe_revision()},
         {"asset_id", map.asset()},
         {"source_digest", ""},
         {"tile_set_digest", asset_detail::content_digest(definitions[0].bytes)}},
        {{"source.bin", snapshot(map.document.dump())}, definitions[0]}};
    map_request.payload["source_digest"] =
        asset_detail::content_digest(map_request.inputs[0].bytes);
    const auto output = asset_detail::execute_tile_recipe(map_request);
    require(decode_cooked_tile_map(output[0].bytes, map.asset()).occupied() == 3,
            "Direct fixed Tile Map recipe lost cells");
    for (const auto* original : {&set_request, &map_request}) {
        std::stop_source cancelled;
        cancelled.request_stop();
        reject([&] { (void)asset_detail::execute_tile_recipe(*original, cancelled.get_token()); });
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            auto bad = *original;
            if (scenario == 0)
                bad.payload["revision"] = "wrong";
            if (scenario == 1)
                bad.payload["source_digest"] = std::string(64, '0');
            if (scenario == 2)
                bad.payload["asset_id"] = AssetId::generate();
            if (scenario == 3)
                bad.payload["unrecognized"] = true;
            reject([&] { (void)asset_detail::execute_tile_recipe(bad); });
        }
    }
    auto bad = map_request;
    bad.inputs.pop_back();
    reject([&] { (void)asset_detail::execute_tile_recipe(bad); });
    bad = map_request;
    bad.payload["tile_set_digest"] = std::string(64, '0');
    reject([&] { (void)asset_detail::execute_tile_recipe(bad); });
    bad = map_request;
    auto missing = map;
    const std::array<TileMapCellEdit, 1> invalid{{{1, {}, {999, 0}}}};
    missing.document = missing.document.paint(invalid);
    bad.inputs[0].bytes = snapshot(missing.document.dump());
    bad.payload["source_digest"] = asset_detail::content_digest(bad.inputs[0].bytes);
    reject([&] { (void)asset_detail::execute_tile_recipe(bad); });
}
void runtime_leases(const std::filesystem::path& root, AssetId set_id, AssetId map_id,
                    bool test_replacement) {
    const auto saved = AssetCatalog::open_project(root).document();
    ResourceLease<TileSetAsset> retained_set;
    ResourceLease<TileMapAsset> retained_map;
    std::shared_ptr<RuntimeResourceService> stopped;
    {
        EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
        auto resources = engine.services().resources();
        stopped = resources;
        Scene scene(engine.world());
        const auto before = scene.snapshot();
        require(engine.services().available(Capability::Resources) &&
                    !engine.services().available(Capability::Rendering),
                "Tile CPU resources required a renderer");
        const auto set = resources->request(RuntimeResourceKind::TileSet, set_id);
        const auto map = resources->request(RuntimeResourceKind::TileMap, map_id);
        const auto second = resources->request(RuntimeResourceKind::TileMap, map_id);
        require(!resources->tile_set(set) && !resources->tile_map(map),
                "Unadopted tile subscription exposed CPU data");
        const auto wait = [&](std::uint64_t token, const auto& ready) {
            const auto deadline = std::chrono::steady_clock::now() + 10s;
            for (;;) {
                resources->synchronize();
                const auto status = resources->inspect(token);
                if (ready(status))
                    return status;
                require(std::chrono::steady_clock::now() < deadline,
                        "Runtime tile subscription stalled: " + status.state + " " +
                            status.diagnostic);
                std::this_thread::sleep_for(1ms);
            }
        };
        const auto ready = [](const auto& status) {
            return status.state == "ready" && !status.retained_revision.empty();
        };
        wait(set, ready);
        const auto original = wait(map, ready);
        wait(second, ready);
        retained_set = resources->tile_set(set);
        retained_map = resources->tile_map(map);
        require(retained_set && retained_map && retained_map->occupied() == 3 &&
                    &retained_map.get() == &resources->tile_map(second).get() &&
                    retained_map->tile_set() == retained_set->asset(),
                "Runtime tile resources lost typed data or coalesced leases");
        require(!resources->tile_map(set) && !resources->tile_set(map),
                "Runtime tile subscription exposed another resource type");
        reject([&] { (void)resources->request(RuntimeResourceKind::TileMap, set_id); });
        reject([&] {
            (void)resources->request(RuntimeResourceKind::TileSet,
                                     engine_texture(EngineTexture::Checker).id);
        });
        reject([&] {
            (void)resources->request(RuntimeResourceKind::TileMap, map_id,
                                     RuntimeTextureVariant::Color);
        });
        require(std::async(std::launch::async,
                           [&] {
                               try {
                                   (void)resources->tile_map(map);
                               } catch (const std::exception&) {
                                   return true;
                               }
                               return false;
                           })
                    .get(),
                "Off-thread runtime tile lease access was accepted");
        {
            EngineContext other(WorldRole::Runtime, false, {runtime_resources_module(root)});
            reject([&] { (void)other.services().resources()->tile_map(map); });
        }
        if (test_replacement) {
            auto catalog = AssetCatalog::open_project(root);
            auto record = catalog.records().at(map_id);
            auto& selected = record.metadata["forge.import"];
            const auto generation = selected.at("generation").get<std::uint64_t>();
            selected["generation"] = generation + 1;
            selected["key"] = std::string(64, 'f');
            catalog.replace(record);
            catalog.save(AssetCatalog::project_index(root));
            resources->refresh();
            const auto failed =
                wait(map, [](const auto& status) { return status.state == "failed"; });
            require(failed.retained_revision == original.retained_revision &&
                        !failed.diagnostic.empty() &&
                        &retained_map.get() == &resources->tile_map(map).get(),
                    "Failed tile replacement discarded the last-good CPU lease");
            catalog.restore(saved);
            record = catalog.records().at(map_id);
            record.metadata["forge.import"]["generation"] = generation + 2;
            catalog.replace(record);
            catalog.save(AssetCatalog::project_index(root));
            resources->refresh();
            wait(map, [&](const auto& status) {
                return ready(status) && status.source_generation == generation + 2;
            });
            require(resources->tile_map(map)->cell(1, {-1, -1}) == TileMapCell{2, 0},
                    "Repaired runtime tile selection changed admitted cells");
        }
        require(resources->release(map) && !resources->release(map) && resources->tile_map(second),
                "Releasing one observer cancelled another tile consumer");
        reject([&] { (void)resources->tile_map(map); });
        require(scene.snapshot() == before,
                "Runtime tile resource access changed authored world data");
    }
    require(!retained_map && !retained_set, "Tile leases remained accessible after owner shutdown");
    reject([&] { (void)stopped->tile_map(1); });
    if (test_replacement) {
        AssetCatalog restore(root);
        restore.restore(saved);
        restore.save(AssetCatalog::project_index(root));
    }
}
void automatic_updates(const std::filesystem::path& root, std::shared_ptr<ProjectLease> lease,
                       const std::filesystem::path& worker, AssetId material_id, AssetId set_id,
                       AssetId map_id) {
    auto registry = tile_asset_import_registry(worker);
    AssetReimportService automatic(
        lease,
        {{"forge.material.builtin",
          {"linux", "none", "cpu"},
          material_import_registry(),
          [](auto& c, const auto& p, const auto&) { prepare_material_publication(c, p); }},
         {tile_set_importer_id,
          {"linux", "none", "cpu"},
          registry,
          [](auto& c, const auto& p, const auto&) { prepare_tile_set_publication(c, p); }},
         {tile_map_importer_id,
          {"linux", "none", "cpu"},
          registry,
          [](auto& c, const auto& p, const auto&) { prepare_tile_map_publication(c, p); }}},
        {}, 10min, 20ms);
    std::vector<AssetImportOutcome> outcomes;
    const auto poll = [&] {
        for (auto& outcome : automatic.poll())
            outcomes.push_back(std::move(outcome));
    };
    const auto finish = [&] {
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        do {
            poll();
            require(std::chrono::steady_clock::now() < deadline, "Tile reimport did not settle");
            std::this_thread::sleep_for(1ms);
        } while (!automatic.complete() || !automatic.settled() || automatic.scanning());
    };
    finish();
    require(outcomes.empty(), "Unchanged tile assets rebuilt at watcher startup");
    const auto before = automatic.catalog()->document();
    auto source = MaterialSource::parse(
        asset_detail::read_bytes(root / "Assets/Atlas.material.json", material_source_byte_limit));
    source.document["overrides"]["double_sided"] = true;
    atomic_write(root / "Assets/Atlas.material.json", source.document.dump());
    bool blocked = true;
    automatic.blocked = [&](AssetId id) { return blocked && id == material_id; };
    automatic.rescan();
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!automatic.queued()) {
        poll();
        require(std::chrono::steady_clock::now() < deadline,
                "Changed image Material did not queue");
        std::this_thread::sleep_for(1ms);
    }
    require(outcomes.empty() && automatic.catalog()->document() == before,
            "Blocked source draft changed tile publications");
    blocked = false;
    finish();
    require(outcomes.size() == 3 && outcomes[0].job.asset == material_id &&
                outcomes[1].job.asset == set_id && outcomes[2].job.asset == map_id,
            "Tile dependency chain did not rebuild Material, Set, Map exactly once in order");
    for (const auto& outcome : outcomes)
        require(outcome.published,
                "Automatic tile dependency rebuild failed: " + outcome.diagnostic);
    require(load_map(root, automatic.catalog(), map_id).occupied() == 3,
            "Automatic dependency recook changed map cells");
}
void file_workflows(const std::filesystem::path& root, std::shared_ptr<ProjectLease> lease,
                    const std::filesystem::path& worker, AssetId material_id, AssetId set_id,
                    AssetId map_id) {
    const auto schema = nlohmann::json{{"components", nlohmann::json::array()}};
    auto catalog = AssetCatalog::open_project(root);
    auto sources = scan_asset_sources(root);
    require(sources.complete &&
                sources.files.at("Assets/Original.tileset.json").source_kind == "tile_set" &&
                sources.files.at("Assets/Original.tilemap.json").source_kind == "tile_map",
            "Tile sources were not recognized by ordinary discovery");
    const auto content = ContentIndex::build(catalog, &sources);
    require(content.types.contains(TileSetAsset::type) &&
                content.types.contains(TileMapAsset::type),
            "Published tile assets are absent from Content");
    const auto impact = scan_asset_references(root, schema, {material_id, set_id});
    require(impact.references.size() == 2 && !impact.uninspected.empty(),
            "Known tile references or opaque source coverage disappeared");
    auto definitions =
        TileSetSource::parse(asset_detail::read_bytes(root / "Assets/Original.tileset.json",
                                                      tile_set_source_byte_limit))
            .document;
    auto world = nlohmann::json::parse(
        TileMapSource::parse(asset_detail::read_bytes(root / "Assets/Original.tilemap.json",
                                                      tile_map_source_byte_limit))
            .document.dump());
    // Valid UUID text in extension data is still opaque, never a guessed reference.
    definitions["opaque"] = material_id;
    definitions["images"][0]["opaque"] = set_id;
    definitions["images"][0]["grid"]["opaque"] = material_id;
    definitions["tiles"][0]["opaque"] = set_id;
    world["opaque"] = set_id;
    world["layers"][0]["opaque"] = material_id;
    auto& chunk = world["chunks"].begin().value();
    chunk["opaque"] = set_id;
    for (auto& cell : chunk["cells"])
        if (!cell.is_null()) {
            cell["opaque"] = material_id;
            break;
        }
    const std::array documents{AssetReferenceDocument{"Assets/Original.tileset.json", definitions},
                               AssetReferenceDocument{"Assets/Original.tilemap.json", world}};
    const auto nested = inspect_asset_references(schema, documents, {material_id, set_id});
    require(nested.references.size() == 2 && nested.uninspected.size() == 8,
            "Nested tile extension UUIDs were guessed as references or lost opaque coverage");
    require(std::any_of(nested.references.begin(), nested.references.end(),
                        [&](const auto& hit) {
                            return hit.target == material_id &&
                                   hit.expected_type == MaterialAsset::type;
                        }) &&
                std::any_of(nested.references.begin(), nested.references.end(),
                            [&](const auto& hit) {
                                return hit.target == set_id &&
                                       hit.expected_type == TileSetAsset::type;
                            }),
            "Known tile references lost their required types");
    const auto broken = root / "Assets/Invalid.tilemap.json";
    atomic_write(broken, R"({"format":"forge.tile-map","format":"forge.tile-map"})");
    reject([&] { (void)scan_asset_references(root, schema, {set_id}); });
    std::filesystem::remove(broken);
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    for (const auto id : {set_id, map_id}) {
        const bool map = id == map_id;
        const std::filesystem::path destination =
            map ? "Assets/Copy.tilemap.json" : "Assets/Copy.tileset.json";
        auto plan = prepare_asset_file_operation(
            root, {AssetFileAction::Duplicate, id, destination}, project_asset_file_rewriter(root));
        require(plan.result != id && plan.duplicated.at(id) == plan.result,
                "Tile duplication reused a root identity");
        (void)AssetFileTransaction(*lease).commit(std::move(plan.changes), false);
        const auto duplicate = AssetCatalog::open_project(root).records().at(plan.result);
        require(!duplicate.metadata.contains("forge.import") &&
                    !duplicate.metadata.contains(map ? "forge.tile_map" : "forge.tile_set"),
                "Tile duplicate claimed an original cooked selection");
        const auto outcome = publish(importer, destination, plan.result, map);
        require(outcome.published, "Tile duplicate did not import: " + outcome.diagnostic);
        auto selected = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
        if (map) {
            require(load_map(root, selected, plan.result).tile_set() == set_id &&
                        load_map(root, selected, plan.result).cell(1, {-1, -1}) ==
                            TileMapCell{2, 0},
                    "Map duplicate changed external definitions or scoped cells");
            auto move = prepare_asset_file_operation(
                root, {AssetFileAction::Move, plan.result, "Assets/Moved.tilemap.json"},
                project_asset_file_rewriter(root));
            (void)AssetFileTransaction(*lease).commit(std::move(move.changes), false);
            require(!std::filesystem::exists(root / destination) &&
                        AssetCatalog::open_project(root).records().at(plan.result).source ==
                            "Assets/Moved.tilemap.json",
                    "Tile Map move did not preserve its catalog identity");
        } else {
            const auto bytes =
                asset_detail::read_bytes(root / destination, tile_set_source_byte_limit);
            const auto source = TileSetSource::parse(bytes);
            require(source.values().tiles[0].id == 2 && source.materials()[0].id == material_id &&
                        source.document.at("opaque").at("preserve") == UINT64_MAX,
                    "Tile Set copy changed scoped keys, external Material or opaque data");
        }
        reject([&] {
            (void)prepare_asset_file_operation(root,
                                               {AssetFileAction::Move, id, "Assets/Wrong.json"},
                                               project_asset_file_rewriter(root));
        });
    }
    auto large = TileMapSource::parse(asset_detail::read_bytes(
        root / "Assets/Original.tilemap.json", tile_map_source_byte_limit));
    auto metadata = large.document.metadata();
    const auto large_id = AssetId::generate();
    metadata["asset_id"] = large_id;
    metadata["opaque"] = std::string(9 * 1024 * 1024, 'x');
    large.document = large.document.with_metadata(std::move(metadata));
    atomic_write(root / "Assets/Large.tilemap.json", large.document.dump());
    catalog = AssetCatalog::open_project(root);
    catalog.add({large_id, TileMapAsset::type, "Assets/Large.tilemap.json"});
    catalog.save(AssetCatalog::project_index(root));
    auto plan = prepare_asset_file_operation(
        root, {AssetFileAction::Duplicate, large_id, "Assets/LargeCopy.tilemap.json"},
        project_asset_file_rewriter(root));
    (void)AssetFileTransaction(*lease).commit(std::move(plan.changes), false);
    const auto copied = TileMapSource::parse(asset_detail::read_bytes(
        root / "Assets/LargeCopy.tilemap.json", tile_map_source_byte_limit));
    require(copied.asset() == plan.result &&
                copied.document.metadata().at("opaque").get_ref<const std::string&>().size() ==
                    9 * 1024 * 1024 &&
                copied.document.cell(1, {-1, -1}) == TileMapCell{2, 0},
            "Map beyond 8MiB lost identity, unknown data or cells during copy");
    const auto large_impact = scan_asset_references(root, schema, {set_id});
    require(large_impact.references.size() == 4 && large_impact.uninspected.size() >= 2,
            "Large map references or opaque payloads escaped file-operation review");
}
void workflow(const std::filesystem::path& root, const std::filesystem::path& worker) {
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    const auto image_material = material(root, lease);
    auto set = TileSetDocument::create(lease, "Assets/Original.tileset.json");
    const auto set_id = set->source().asset();
    set->edit(set->revision(), "Original sheet and definitions", [&](auto& source) {
        source["next_id"] = 6;
        source["images"].push_back({{"id", 1},
                                    {"name", "Original checker sheet"},
                                    {"material", image_material},
                                    {"grid",
                                     {{"columns", 2},
                                      {"rows", 2},
                                      {"margin_x", 0},
                                      {"margin_y", 0},
                                      {"spacing_x", 0},
                                      {"spacing_y", 0}}}});
        for (unsigned n = 0; n < 4; ++n)
            source["tiles"].push_back(
                {{"id", n + 2}, {"name", "Tile " + std::to_string(n)}, {"image", 1}, {"frame", n}});
        source["opaque"] = {{"preserve", UINT64_MAX}};
    });
    set->save();
    AssetImportService service(lease, tile_asset_import_registry(worker), {"linux", "none", "cpu"});
    auto done = publish(service, set->locator(), set_id, false);
    require(done.published, "Tile Set worker publication failed: " + done.diagnostic);
    auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto first_set = load_set(root, catalog, set_id);
    require(first_set.tiles().size() == 4 && first_set.image(1).grid.columns == 2 &&
                catalog->records().at(set_id).dependency_edges.size() == 2,
            "Published Tile Set values or typed Material dependency differ");
    done = publish(service, set->locator(), set_id, false);
    require(done.published && done.cache_hit,
            "Unchanged Tile Set did not reuse its immutable candidate");
    auto map = TileMapDocument::create(lease, "Assets/Original.tilemap.json", [&](AssetId id) {
        return TileMapSource{TileMapValue::create(id, set_id)};
    });
    const auto map_id = map->source().asset();
    const std::array<TileMapCellEdit, 3> cells{
        {{1, {-1, -1}, {2, 0}}, {1, {32, 0}, {3, 4}}, {1, {INT32_MAX, INT32_MIN}, {5, 7}}}};
    map->edit_source(map->revision(), "Original sparse cells", [&](auto& candidate) {
        candidate.document = candidate.document.paint(cells);
    });
    map->save();
    recipe_guards(set->source(), map->source());
    done = publish(service, map->locator(), map_id, true);
    require(done.published, "Tile Map worker publication failed: " + done.diagnostic);
    catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    auto map_data = load_map(root, catalog, map_id);
    require(map_data.occupied() == 3 && map_data.tile_set() == set_id &&
                map_data.cell(1, {INT32_MAX, INT32_MIN}) == TileMapCell{5, 7} &&
                catalog->records().at(map_id).dependency_edges.size() == 2,
            "Published sparse map values or typed Tile Set dependency differ");
    runtime_leases(root, set_id, map_id, true);
    const auto good_catalog = catalog->document();
    set->edit(set->revision(), "Grid that cannot divide its real texture",
              [](auto& source) { source["images"][0]["grid"]["columns"] = 3; });
    set->save();
    done = publish(service, set->locator(), set_id, false);
    require(!done.published && !done.diagnostic.empty() &&
                AssetCatalog::open_project(root).document() == good_catalog,
            "Invalid actual image layout replaced last-good publications");
    set->undo();
    set->save();
    map->edit_source(map->revision(), "Missing tile key", [](auto& candidate) {
        const std::array<TileMapCellEdit, 1> invalid{{{1, {0, 0}, {999, 0}}}};
        candidate.document = candidate.document.paint(invalid);
    });
    map->save();
    done = publish(service, map->locator(), map_id, true);
    require(!done.published && !done.diagnostic.empty() &&
                AssetCatalog::open_project(root).document() == good_catalog,
            "Map with a missing Tile Set key replaced last-good publications");
    map->undo();
    map->save();
    ResourcePool<TileSetAsset> held({1, 4, 8, 8 * 1024 * 1024});
    const auto old_ticket = request_tile_set(held, root, catalog, {set_id});
    require(held.wait(old_ticket, 10s), "Initial retained Tile Set did not load");
    const auto old = held.acquire(old_ticket);
    set->edit(set->revision(), "Rename stable tile",
              [](auto& source) { source["tiles"][0]["name"] = "Renamed"; });
    set->save();
    done = publish(service, set->locator(), set_id, false);
    require(done.published, "Renamed Tile Set did not publish: " + done.diagnostic);
    catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto next_ticket = request_tile_set(held, root, catalog, {set_id});
    require(held.wait(next_ticket, 10s), "Replacement Tile Set did not load");
    const auto next = held.acquire(next_ticket);
    require(next && old && next->tile(2).name == "Renamed" && old->tile(2).name == "Tile 0" &&
                next.identity().revision != old.identity().revision,
            "Tile Set replacement altered a retained old revision or changed stable IDs");
    reject([&] { (void)load_map(root, catalog, map_id); });
    done = publish(service, map->locator(), map_id, true);
    require(done.published, "Map did not recook against the new Tile Set: " + done.diagnostic);
    catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    map_data = load_map(root, catalog, map_id);
    require(map_data.cell(1, {-1, -1}).tile == 2, "Recook remapped a stable tile key");
    automatic_updates(root, lease, worker, image_material, set_id, map_id);
    file_workflows(root, lease, worker, image_material, set_id, map_id);
    catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto package = root / "package";
    const std::array roots{map_id};
    (void)package_runtime_content(root, package, roots, {"linux", "none"});
    const auto moved = root.parent_path() / "relocated cooked content";
    std::filesystem::rename(package, moved);
    std::filesystem::remove_all(root / "Assets");
    const auto packaged =
        std::make_shared<const AssetCatalog>(open_runtime_content(moved, {"linux", "none"}));
    runtime_leases(moved, set_id, map_id, false);
    require(load_set(moved, packaged, set_id).tile(2).name == "Renamed" &&
                load_map(moved, packaged, map_id).cell(1, {-1, -1}) == TileMapCell{2, 0} &&
                !std::filesystem::exists(moved / "Assets/Original.tilemap.json"),
            "Relocated source-free tile package differs or shipped its authored map");
    require(load_map(root, catalog, map_id).occupied() == 3 &&
                load_set(root, catalog, set_id).tiles().size() == 4,
            "Cooked tile resource tried to read an offline authoring source");
    for (const auto id : {set_id, map_id}) {
        auto altered = *packaged;
        auto record = altered.records().at(id);
        record.metadata[id == set_id ? "forge.tile_set" : "forge.tile_map"]["sha256"] =
            std::string(64, '0');
        altered.replace(record);
        reject([&] {
            if (id == set_id)
                (void)load_set(moved, std::make_shared<const AssetCatalog>(altered), id);
            else
                (void)load_map(moved, std::make_shared<const AssetCatalog>(altered), id);
        });
    }
    std::cout << "Actual tile worker publication, image/reference validation, immutable "
                 "replacement and relocated source-free package passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected native import worker and dedicated scratch root");
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        workflow(root / "project with spaces", std::filesystem::absolute(argv[1]));
        std::filesystem::remove_all(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
