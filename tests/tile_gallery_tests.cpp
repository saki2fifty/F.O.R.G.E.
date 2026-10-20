#include "../samples/projects/FeatureGallery/CodeExamples/TileMapEdits.hpp"
#include "asset_bytes.hpp"
#include "runtime_package.hpp"
#include "runtime_tile_map.hpp"
#include "tile_map_document.hpp"
#include "tile_set_source.hpp"
#include <forge/game_content.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
AssetId find(const AssetCatalog& catalog, std::string_view source) {
    for (const auto& [id, record] : catalog.records())
        if (!record.subasset && path_utf8(record.source) == source)
            return id;
    throw std::runtime_error("Tile Gallery asset missing: " + std::string(source));
}
void runtime(const std::filesystem::path& root, AssetId scene_id, AssetId map_id) {
    EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {scene_id}));
    const auto authored = scene.document();
    RuntimeSimulation simulation(engine.world(), scene);
    RenderScene frame;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        frame = extract_render_scene(simulation.presentation(1));
        detail::bind_runtime_tile_maps(engine.world(), frame);
        std::string diagnostic;
        for (const auto& map : frame.tile_maps)
            if (!map.diagnostic.empty())
                diagnostic += "; " + map.diagnostic;
        check(std::chrono::steady_clock::now() < end,
              "Gallery native map loading timed out" + diagnostic);
        if (frame.tile_maps.size() != 2 || !frame.tile_maps[0].cells || !frame.tile_maps[1].cells)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (frame.tile_maps.size() != 2 || !frame.tile_maps[0].cells || !frame.tile_maps[1].cells);
    check(frame.diagnostics.empty() && frame.cameras.size() == 1 && frame.meshes.empty(),
          "Tile Gallery lost its resolved map/camera profile");
    bool mirrored = false;
    for (const auto& map : frame.tile_maps) {
        check(map.runtime_instance && map.renderer.map.id == map_id &&
                  map.cells->occupied() == 40 && map.cells->chunk_count() == 6 &&
                  map.cells->cell(1, {-6, 2}).tile == 3 && map.cells->cell(2, {-6, 2}).tile == 2 &&
                  map.cells->layers().size() == 2,
              "Gallery map lost scoped definitions, layers or negative cell chunks");
        mirrored |= map.world.m[0] < 0;
    }
    check(mirrored, "Gallery copy lost its reflected instance transform");
    const auto old = frame;
    const auto target = EntityRef{scene_id, frame.tile_maps[0].entity};
    const std::array<TileMapCellEdit, 1> write{{{1, {0, 1}, {2, 0}}}};
    const auto token = detail::request_runtime_tile_cells(
        engine.world(), "project.gallery_test", target, frame.tile_maps[0].generation, write);
    (void)simulation.presentation(1);
    check(detail::inspect_runtime_tile_edit(engine.world(), "project.gallery_test", token).state ==
              detail::RuntimeTileEditState::Pending,
          "Gallery paused presentation published a cell edit");
    simulation.tick(1.f / 60.f);
    frame = extract_render_scene(simulation.presentation(1));
    detail::bind_runtime_tile_maps(engine.world(), frame);
    check(frame.tile_maps[0].cells->cell(1, {0, 1}).tile == 2 &&
              !frame.tile_maps[1].cells->cell(1, {0, 1}).tile &&
              !old.tile_maps[0].cells->cell(1, {0, 1}).tile && scene.document() == authored,
          "Gallery runtime edit changed its other instance, detached frame or source Scene");
    check(detail::release_runtime_tile_edit(engine.world(), "project.gallery_test", token),
          "Gallery runtime edit token was not released");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery project and scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "private authored Gallery";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(std::filesystem::absolute(argv[1]), root,
                              std::filesystem::copy_options::recursive);
        const auto catalog = AssetCatalog::open_project(root);
        const auto scene = find(catalog, "Scenes/74-tile-maps.scene.json");
        const std::array roots{scene};
        const auto map = find(catalog, "Assets/Tiles/ArrowCourse.tilemap.json");
        const auto set = find(catalog, "Assets/Tiles/Directions.tileset.json");
        const auto material = find(catalog, "Assets/Sprites/SignalSheet.material.json");
        const auto texture = find(catalog, "Assets/Sprites/SignalSheet.png");
        const auto definitions = TileSetSource::parse(asset_detail::read_bytes(
            root / "Assets/Tiles/Directions.tileset.json", tile_set_source_byte_limit));
        const auto values = definitions.values();
        check(values.asset == set && values.images.size() == 1 && values.tiles.size() == 4 &&
                  values.images[0].material.id == material && values.images[0].grid.columns == 4,
              "Gallery palette lost its original image material/definitions");
        {
            auto lease = std::make_shared<ProjectLease>(root);
            TileMapDocument document(lease, "Assets/Tiles/ArrowCourse.tilemap.json");
            const auto saved = document.source();
            const std::array<TileMapCellEdit, 1> cells{{{1, {0, 1}, {2, 0}}}};
            document.edit_source(
                document.revision(), "Gallery paint experiment",
                [&](auto& source) { source.document = source.document.paint(cells); });
            check(document.source().document.cell(1, {0, 1}).tile == 2 && document.can_undo(),
                  "Gallery source document did not retain its paint edit");
            document.undo();
            check(document.source().document == saved.document && document.can_redo(),
                  "Gallery paint Undo lost the original cells");
            document.redo();
            check(document.source().document.cell(1, {0, 1}).tile == 2,
                  "Gallery paint Redo lost the new cell");
            document.undo();
            document.save();
            WorldContext author;
            Scene schema_owner(author);
            prepare_runtime_content_catalog(*lease, roots, {}, schema_owner.schema());
        }
        runtime(root, scene, map);
        const auto package = scratch / "cooked tile content";
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        std::filesystem::remove_all(root);
        const auto moved = scratch / "relocated source-free content";
        std::filesystem::rename(package, moved);
        const auto packed = open_runtime_content(moved, {"windows", "d3d12"});
        check(packed.records().size() == 5 && packed.records().contains(scene) &&
                  packed.records().contains(map) && packed.records().contains(set) &&
                  packed.records().contains(material) && packed.records().contains(texture),
              "Tile package dependency closure lost identity or pulled unrelated Gallery assets");
        for (const auto& entry : std::filesystem::recursive_directory_iterator(moved)) {
            const auto name = path_utf8(entry.path().filename());
            check(!name.ends_with(".png") && !name.ends_with(".material.json") &&
                      !name.ends_with(".tilemap.json") && !name.ends_with(".tileset.json") &&
                      !name.ends_with(".cpp") && !name.ends_with(".hpp"),
                  "Tile runtime content leaked authored source");
        }
        runtime(moved, scene, map);
        std::filesystem::remove_all(scratch);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
