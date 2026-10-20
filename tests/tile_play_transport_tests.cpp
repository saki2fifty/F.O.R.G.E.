#include "editor/play.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include <SDL3/SDL.h>
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/render_scene.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
Json publish_scene(const std::filesystem::path& root, const std::filesystem::path& worker) {
    std::filesystem::create_directories(root / "Assets");
    auto settings = ProjectSettings::defaults("Tile Play transport");
    settings["game"] = default_game_settings("org.forge.tile-play-" + AssetId::generate().str(),
                                             "Tile Play transport");
    atomic_write(root / "forge.project.json", settings.dump(2));
    auto lease = std::make_shared<ProjectLease>(root);
    const auto material = test_tile_asset_io::material(root, lease);
    auto definitions = TileSetDocument::create(lease, "Assets/Original.tileset.json");
    const auto set_id = definitions->source().asset();
    definitions->edit(definitions->revision(), "Original single-frame sheet", [&](auto& source) {
        source["next_id"] = 3;
        source["images"] = {{{"id", 1},
                             {"name", "Original"},
                             {"material", material},
                             {"grid",
                              {{"columns", 1},
                               {"rows", 1},
                               {"margin_x", 0},
                               {"margin_y", 0},
                               {"spacing_x", 0},
                               {"spacing_y", 0}}}}};
        source["tiles"] = {{{"id", 2}, {"name", "Cell"}, {"image", 1}, {"frame", 0}}};
    });
    definitions->save();
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                {"linux", "none", "cpu"});
    auto published = test_tile_asset_io::publish(importer, definitions->locator(), set_id, false);
    check(published.published, "Set publication failed: " + published.diagnostic);
    auto map = TileMapDocument::create(lease, "Assets/Original.tilemap.json", [&](AssetId id) {
        return TileMapSource{TileMapValue::create(id, set_id)};
    });
    const auto map_id = map->source().asset();
    std::vector<TileMapCellEdit> cells;
    for (int x = -65; x < 66; x += 16)
        cells.push_back({1, {x, 0}, {2, 0}});
    map->edit_source(map->revision(), "Cross-chunk cells",
                     [&](auto& source) { source.document = source.document.paint(cells); });
    map->save();
    published = test_tile_asset_io::publish(importer, map->locator(), map_id, true);
    check(published.published, "Map publication failed: " + published.diagnostic);
    WorldContext world;
    Scene scene(world);
    auto document = scene.document();
    Json components = Json::object();
    const auto schema = scene.schema();
    for (const auto& type : schema.at("components"))
        if (type.at("id") == "forge.tile_map_renderer" ||
            type.at("id") == "forge.local_translation" || type.at("id") == "forge.local_rotation" ||
            type.at("id") == "forge.local_scale")
            for (const auto& field : type.at("fields"))
                components[type.at("id").get<std::string>()][field.at("id").get<std::string>()] =
                    field.at("default");
    components["forge.tile_map_renderer"]["map"] = map_id;
    for (unsigned i = 0; i != 2; ++i)
        document["entities"].push_back(
            {{"id", EntityId::generate()}, {"name", "Map"}, {"components", components}});
    scene.reset(document);
    return scene.document();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 5, "Need runtime, import worker, scratch root and SDK build ON/OFF");
        check(SDL_Init(0), "SDL initialization failed");
        const auto root = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        const auto document = publish_scene(root, std::filesystem::absolute(argv[2]));
        auto catalog = AssetCatalog::open_project(root).document();
        const auto original_source = asset_detail::read_bytes(root / "Assets/Original.tilemap.json",
                                                              tile_map_source_byte_limit);
        auto publish_cell = [&](TileMapCell value) {
            auto lease = std::make_shared<ProjectLease>(root);
            TileMapDocument map(lease, "Assets/Original.tilemap.json");
            const std::array<TileMapCellEdit, 1> cells{{{1, {-65, 0}, value}}};
            map.edit_source(map.revision(), "Replace published cell",
                            [&](auto& source) { source.document = source.document.paint(cells); });
            map.save();
            AssetImportService importer(lease, tile_asset_import_registry(argv[2]),
                                        {"linux", "none", "cpu"});
            const auto outcome =
                test_tile_asset_io::publish(importer, map.locator(), map.source().asset(), true);
            check(outcome.published, "Map replacement failed: " + outcome.diagnostic);
            catalog = AssetCatalog::open_project(root).document();
        };
        for (const bool sdk : {false, true}) {
            if (sdk && std::string_view(argv[4]) == "OFF")
                continue;
            PlaySession play;
            // The fixture uses both host assemblies and has no native modules.
            // SDK Play requires the exact-SDK launch policy even in this fixture.
            play.configure(60, InputMap{}, {0, -9.81, 0}, root, sdk, sdk);
            play.set_user_data_override(root / "users");
            play.set_headless_audio(true);
            auto wait = [&](auto done) {
                const auto end = SDL_GetTicks() + 15000;
                while (!done() && play.active() && SDL_GetTicks() < end) {
                    play.pump();
                    SDL_Delay(1);
                }
                check(done(), "Tile Play timed out: " + play.status() + " " + play.log());
            };
            auto bound = [&](std::uint64_t ticket = 0) {
                auto scene = extract_render_scene(ticket ? play.sdk_candidate_envelope().at("scene")
                                                         : play.effective_snapshot());
                play.bind_tile_maps(scene, ticket);
                return scene;
            };
            auto copied = [&](std::uint64_t ticket = 0) {
                const auto scene = bound(ticket);
                return scene.tile_maps.size() == 2 &&
                       std::all_of(scene.tile_maps.begin(), scene.tile_maps.end(),
                                   [](const auto& map) {
                                       return map.runtime_instance && map.cells &&
                                              map.definitions && !map.pending;
                                   });
            };
            play.start(argv[1], document, true);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                const auto ticket = play.sdk_candidate_ticket();
                wait([&] { return copied(ticket); });
                check(!play.ready(), "SDK tile candidate activated before presentation admission");
                check(play.submit_editor_observation(false, "KeyboardMouse"),
                      "Editor release observation failed");
                check(play.submit_sdk_candidate_ack(true, play.session(), ticket),
                      "Copied tile candidate acknowledgement failed");
            }
            wait([&] { return play.control_ready() && copied(); });
            const auto retained = bound();
            for (const auto& map : retained.tile_maps)
                check(map.cells->occupied() == 9 && map.cells->cell(1, {-65, 0}).tile == 2,
                      "Real process copied different cross-chunk tile contents");
            play.pause();
            wait([&] { return play.paused() && play.control_ready(); });
            const auto tick = play.timing().at("tick").get<std::uint64_t>();
            play.step();
            wait([&] { return play.control_ready() && play.timing().at("tick") == tick + 1; });
            check(copied(), "Control scheduling dropped copied tiles");
            publish_cell({});
            play.model_assets_changed();
            wait([&] { return copied() && bound().tile_maps[0].cells->occupied() == 8; });
            check(retained.tile_maps[0].cells->occupied() == 9 &&
                      bound().tile_maps[0].generation != retained.tile_maps[0].generation,
                  "Published source replacement changed an old frame or reused its generation");
            play.stop();
            check(!bound().tile_maps[0].cells && retained.tile_maps[0].cells->occupied() == 9,
                  "Stop retained current cells or invalidated a detached frame");
            publish_cell({2, 0});
            check(asset_detail::read_bytes(root / "Assets/Original.tilemap.json",
                                           tile_map_source_byte_limit) == original_source,
                  "Round-trip source replacement changed unrelated authoring values");
            play.start(argv[1], document);
            if (sdk) {
                wait([&] { return !play.sdk_candidate_envelope().is_null(); });
                const auto ticket = play.sdk_candidate_ticket();
                wait([&] { return copied(ticket); });
                check(play.submit_editor_observation(false, "KeyboardMouse") &&
                          play.submit_sdk_candidate_ack(true, play.session(), ticket),
                      "Restarted candidate admission failed");
            }
            wait([&] { return play.control_ready() && copied(); });
            play.stop();
        }
        check(AssetCatalog::open_project(root).document() == catalog,
              "Play transfer modified the authored catalog");
        std::filesystem::remove_all(root);
        SDL_Quit();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
