#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "material_authoring.hpp"
#include "material_selection.hpp"
#include "runtime_package.hpp"
#include "tile_asset_authoring.hpp"
#include "tile_asset_resources.hpp"
#include "tile_asset_worker.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <forge/engine_texture_assets.hpp>
#include <iostream>

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
                catalog->records().at(set_id).dependency_edges.size() == 1,
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
                catalog->records().at(map_id).dependency_edges.size() == 1,
            "Published sparse map values or typed Tile Set dependency differ");
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
    const auto package = root / "package";
    const std::array roots{map_id};
    (void)package_runtime_content(root, package, roots, {"linux", "none"});
    const auto moved = root.parent_path() / "relocated cooked content";
    std::filesystem::rename(package, moved);
    std::filesystem::remove_all(root / "Assets");
    const auto packaged =
        std::make_shared<const AssetCatalog>(open_runtime_content(moved, {"linux", "none"}));
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
