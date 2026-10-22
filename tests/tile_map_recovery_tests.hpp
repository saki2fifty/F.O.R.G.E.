#pragma once
#include "runtime_tile_map.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_asset_worker.hpp"
#include "tile_map_document.hpp"
#include <forge/render_components.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <thread>

// Real published resources and fresh ordinary runtime owners. The enclosing
// TileAssets fixture owns source publication; this helper never changes it.
inline void test_tile_map_recovery(const std::filesystem::path& root, const forge::Json& document,
                                   const forge::Json& saved) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
    Scene scene(engine.world());
    auto source = document;
    auto unused = source.at("entities")[0];
    unused["id"] = EntityId::generate();
    unused["name"] = "Disabled unavailable Map";
    unused["components"]["forge.tile_map_renderer"]["enabled"] = false;
    unused["components"]["forge.tile_map_renderer"]["map"] = AssetId::generate();
    source["entities"].push_back(unused);
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    check(checkpoint_runtime_tile_maps(engine.world()).is_null(),
          "Tile Map checkpoint captured unrealized resources");
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    Json baseline;
    do {
        (void)simulation.presentation(0);
        baseline = checkpoint_runtime_tile_maps(engine.world());
        check(std::chrono::steady_clock::now() < deadline,
              "Fresh Tile Map recovery resource preparation stalled");
        if (baseline.is_null())
            std::this_thread::sleep_for(1ms);
    } while (baseline.is_null());
    check(baseline.at("entries").size() == 2 && saved.at("entries").size() == 2,
          "Tile Map recovery fixture lost its independent instances");
    std::size_t edited = 0;
    for (; edited < saved.at("entries").size(); ++edited)
        if (saved.at("entries")[edited].at("edited").get<bool>())
            break;
    check(edited < 2, "Tile Map recovery fixture has no edited instance");
    const auto reject = [&](const Json& candidate) {
        bool rejected = false;
        try {
            restore_runtime_tile_maps(engine.world(), candidate);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Malformed Tile Map recovery was accepted");
        check(checkpoint_runtime_tile_maps(engine.world()) == baseline &&
                  scene.document() == source,
              "Rejected Tile Map recovery partially changed cells, generations or source");
    };
    auto bad = saved;
    bad["version"] = 2;
    reject(bad);
    bad = saved;
    bad["entries"][1]["map_revision"] = "changed published revision";
    reject(bad); // First instance has already been prepared when the second rejects.
    bad = saved;
    bad["entries"][1]["set_revision"] = "changed definition revision";
    reject(bad);
    bad = saved;
    bad["entries"][1]["entity"] = bad["entries"][0]["entity"];
    reject(bad);
    bad = saved;
    bad["entries"].erase(1);
    reject(bad);
    for (const Json invalid : {Json(0), Json(1.5), Json(true), saved.at("next_generation")}) {
        bad = saved;
        bad["entries"][0]["generation"] = invalid;
        reject(bad);
    }
    bad = saved;
    bad["entries"][1]["generation"] = bad["entries"][0]["generation"];
    reject(bad);
    bad = saved;
    bad["entries"][edited]["cells"]["header"]["map"]["cell_size"][0] = 2;
    reject(bad);
    bad = saved;
    bad["entries"][edited]["cells"]["header"]["definitions"]["images"][0]["material"] =
        AssetId::generate();
    reject(bad);
    bad = saved;
    bad["entries"][edited]["cells"]["header"]["map"]["cell_count"] =
        bad["entries"][edited]["cells"]["header"]["map"]["cell_count"].get<unsigned>() + 1;
    reject(bad);
    bad = saved;
    auto& pages = bad["entries"][edited]["cells"]["pages"];
    check(!pages.empty(), "Edited recovery fixture has no full-capture pages");
    pages.push_back(pages[0]);
    reject(bad);
    bad = saved;
    bad["entries"][edited]["cells"]["pages"][0][0]["cells"][0][1] = 999;
    reject(bad);
    bad = saved;
    bad["entries"][edited]["revision"] = 0;
    reject(bad);
    restore_runtime_tile_maps(engine.world(), saved);
    check(checkpoint_runtime_tile_maps(engine.world()) == saved,
          "Fresh Tile Map recovery lost cells, source bindings, independent generations or "
          "revisions");
    (void)simulation.presentation(0);
    check(checkpoint_runtime_tile_maps(engine.world()) == saved && scene.document() == source,
          "Presentation overwrote recovered cells or modified authored data");
    const auto target = saved.at("entries")[edited].at("entity").get<EntityRef>();
    const auto before = runtime_tile_map_snapshot(engine.world(), target);
    const std::array<TileMapCellEdit, 1> erase{{{1, {}, {}}}};
    const auto token = request_runtime_tile_cells(engine.world(), "recovered.module", target,
                                                  before->generation, erase);
    check(checkpoint_runtime_tile_maps(engine.world()).is_null(),
          "Pending runtime cell command was captured as completed state");
    simulation.tick(1.f / 60);
    const auto after = runtime_tile_map_snapshot(engine.world(), target);
    check(after->generation == before->generation && after->revision == before->revision + 1 &&
              !after->cells.cell(1, {}).tile && before->cells.cell(1, {}).tile,
          "Recovered runtime Map could not continue independent fixed-boundary edits");
    release_runtime_tile_edit(engine.world(), "recovered.module", token);
    const auto reset =
        request_runtime_tile_reset(engine.world(), "recovered.module", target, after->generation);
    simulation.tick(1.f / 60);
    const auto current = runtime_tile_map_snapshot(engine.world(), target);
    check(current->generation == saved.at("next_generation").get<std::uint64_t>() &&
              current->revision == 0 && !current->edited && scene.document() == source,
          "Recovered generation frontier was reused or reset failed to restore published cells");
    release_runtime_tile_edit(engine.world(), "recovered.module", reset);
}

inline void test_tile_map_recovery_memory(const std::filesystem::path& root, forge::Json document,
                                          const forge::Json& saved) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    while (document["entities"].size() < 4) {
        auto entity = document["entities"][0];
        entity["id"] = EntityId::generate();
        document["entities"].push_back(std::move(entity));
    }
    EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(document);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    Json baseline;
    do {
        (void)simulation.presentation(0);
        baseline = checkpoint_runtime_tile_maps(engine.world());
        check(std::chrono::steady_clock::now() < deadline,
              "Sparse recovery memory fixture did not prepare");
        if (baseline.is_null())
            std::this_thread::sleep_for(1ms);
    } while (baseline.is_null());
    Json cells;
    for (const auto& row : saved.at("entries"))
        if (row.at("edited").get<bool>())
            cells = row.at("cells");
    check(!cells.is_null(), "Sparse recovery fixture has no admitted copied header");
    const auto tile = cells["header"]["definitions"]["tiles"][0]["id"];
    cells["header"]["map"]["chunk_count"] = 1024;
    cells["header"]["map"]["cell_count"] = 1024;
    cells["pages"] = Json::array();
    for (int page = 0; page < 256; ++page) {
        Json rows = Json::array();
        for (int i = 0; i < 4; ++i)
            rows.push_back({{"layer", 1},
                            {"x", page * 4 + i},
                            {"y", 0},
                            {"cells", Json::array({Json::array({0, tile, 0})})}});
        cells["pages"].push_back(std::move(rows));
    }
    auto candidate = baseline;
    for (auto& row : candidate["entries"]) {
        row["edited"] = true;
        row["revision"] = 1;
        row["cells"] = cells;
    }
    check(candidate.dump().size() < 1024 * 1024,
          "Sparse memory fixture is not a compact checkpoint");
    bool rejected = false;
    try {
        restore_runtime_tile_maps(engine.world(), candidate);
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("copied cell memory") != std::string::npos;
    }
    check(rejected && checkpoint_runtime_tile_maps(engine.world()) == baseline &&
              scene.document() == document,
          "Compact sparse recovery expanded past its cell budget or partially published");
}

// Capture must budget the independently reconstructed edited arrays, even when
// live copy-on-write instances share almost all of their published source chunks.
inline void test_tile_map_capture_expansion(const std::filesystem::path& root,
                                            std::shared_ptr<forge::ProjectLease> lease,
                                            const std::filesystem::path& worker,
                                            forge::AssetId set) {
    using namespace forge;
    using namespace forge::detail;
    using namespace std::chrono_literals;
    const auto check = [](bool ok, const char* why) {
        if (!ok)
            throw std::runtime_error(why);
    };
    auto map =
        TileMapDocument::create(lease, "Assets/SharedSparseRecovery.tilemap.json", [&](AssetId id) {
            return TileMapSource{TileMapValue::create(id, set)};
        });
    map->edit_source(map->revision(), "One original cell in each bounded source chunk",
                     [](auto& source) {
                         std::vector<TileMapCellEdit> writes;
                         for (int chunk = 0; chunk < 1024; ++chunk)
                             writes.push_back({1, {chunk * 32, 0}, {2, 0}});
                         source.document = source.document.paint(writes);
                     });
    map->save();
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    check(test_tile_asset_io::publish(importer, map->locator(), map->source().asset(), true)
              .published,
          "Shared sparse recovery source did not publish");
    EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
    Scene scene(engine.world());
    auto source = scene.document();
    Json components;
    const auto schema = scene.schema();
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.tile_map_renderer")
            for (const auto& field : type.at("fields"))
                components["forge.tile_map_renderer"][field.at("id").get<std::string>()] =
                    field.at("default");
    components["forge.tile_map_renderer"]["map"] = map->source().asset();
    for (unsigned i = 0; i < 4; ++i)
        source["entities"].push_back({{"id", EntityId::generate()},
                                      {"name", "Shared sparse Map"},
                                      {"components", components}});
    scene.restore_snapshot(source);
    RuntimeSimulation simulation(engine.world(), scene);
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    Json baseline;
    do {
        (void)simulation.presentation(0);
        baseline = checkpoint_runtime_tile_maps(engine.world());
        check(std::chrono::steady_clock::now() < deadline, "Shared sparse Map readiness stalled");
        if (baseline.is_null())
            std::this_thread::sleep_for(1ms);
    } while (baseline.is_null());
    const std::array<TileMapCellEdit, 1> change{{{1, {0, 0}, {2, 1}}}};
    std::vector<std::uint64_t> tokens;
    std::vector<std::shared_ptr<const RuntimeTileMapSnapshot>> snapshots;
    for (const auto& row : source.at("entities")) {
        const auto ref = scene.reference(row.at("id").get<std::string>());
        const auto current = runtime_tile_map_snapshot(engine.world(), ref);
        tokens.push_back(request_runtime_tile_cells(engine.world(), "project.capture_expansion",
                                                    ref, current->generation, change));
    }
    simulation.tick(1.f / 60);
    for (const auto token : tokens) {
        check(inspect_runtime_tile_edit(engine.world(), "project.capture_expansion", token).state ==
                  RuntimeTileEditState::Applied,
              "Shared sparse runtime edit failed");
        release_runtime_tile_edit(engine.world(), "project.capture_expansion", token);
    }
    std::vector<const TileMapData*> retained;
    std::size_t independent = 0;
    for (const auto& row : source.at("entities")) {
        snapshots.push_back(runtime_tile_map_snapshot(
            engine.world(), scene.reference(row.at("id").get<std::string>())));
        retained.push_back(&snapshots.back()->cells);
        independent += snapshots.back()->cells.resident_bytes();
    }
    check(TileMapData::resident_bytes(retained) < 64 * 1024 * 1024 &&
              independent > 64 * 1024 * 1024,
          "Shared sparse fixture did not exercise capture versus reconstruction expansion");
    bool rejected = false;
    try {
        (void)checkpoint_runtime_tile_maps(engine.world());
    } catch (const std::exception& error) {
        rejected = std::string(error.what()).find("copied cell memory") != std::string::npos;
    }
    check(rejected, "Capture accepted shared edited Maps whose fresh reconstruction exceeds64MiB");
}
