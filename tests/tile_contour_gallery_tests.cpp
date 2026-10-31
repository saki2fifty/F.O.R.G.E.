#include "runtime_package.hpp"
#include "runtime_tile_map.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace forge;
namespace {
using test_tile_asset_io::check;
constexpr auto source = "Scenes/123-tile-collision-contours.scene.json";
Json playback(const std::filesystem::path& root, AssetId scene) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Contour Gallery assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::map<std::string, EntityRef> refs;
    EntityRef floor;
    const auto document = world.scene.document();
    for (const auto& row : document.at("entities")) {
        const auto ref = world.scene.reference(row.at("id").get<std::string>());
        refs.emplace(row.at("name").get<std::string>(), ref);
        if (row.at("components").contains("forge.tile_map_renderer"))
            floor = ref;
    }
    check(world.physics()->status().at("bodies_2d") == 5 &&
              world.physics()->status().at("shapes_2d") == 20,
          "Contour Gallery lost compound pieces or its four independent actors: " +
              world.physics()->status().dump());
    const auto snapshot = detail::runtime_tile_map_snapshot(world.engine.world(), floor);
    check(snapshot && snapshot->cells.occupied() == 5 &&
              snapshot->definition_values.tile(2).collision.contours &&
              snapshot->definition_values.tile(2).collision.contours->contours.size() == 2 &&
              snapshot->definition_values.tile(2).collision.contours->contours[0].holes.size() ==
                  1 &&
              snapshot->definition_values.tile(3).collision.contours->contours.size() == 2,
          "Contour Gallery loaded obsolete definitions or logical cells");
    for (const auto& [position, solid] :
         std::array{std::pair{Double2{-2.7, -2}, false}, std::pair{Double2{-2, -2}, true},
                    std::pair{Double2{-3.5, -2}, true}, std::pair{Double2{6, -2}, false},
                    std::pair{Double2{4.5, -2}, true}, std::pair{Double2{7.5, -2}, true}}) {
        PhysicsOverlap2D query;
        query.origin = position;
        query.dimensions = {.02, 0};
        const auto found = world.physics()->overlap_shape_2d(query);
        check(found == (solid ? std::vector<EntityRef>{floor} : std::vector<EntityRef>{}),
              "Contour Gallery filled a void/gap or lost a ring/island/bar");
    }
    const std::array lanes{std::pair{"Amber - outer ring roof", 0.},
                           std::pair{"Cyan - inside the ring void", -3.},
                           std::pair{"Rose - island inside the void", -1.5},
                           std::pair{"Green - between separate bars", -4.}};
    Json samples = Json::array();
    for (unsigned tick = 0; tick <= 180; ++tick) {
        if (tick)
            world.simulation.tick(1.f / 60);
        if (tick == 30 || tick == 90 || tick == 180) {
            Json sample{{"tick", tick}};
            for (const auto& [name, roof] : lanes) {
                const auto state = world.physics()->body_state_2d(refs.at(name));
                check(bool(state), "Contour Gallery actor has no native body");
                sample[name] = state->position;
                if (tick == 180)
                    check(std::abs(state->position[1] - .15 - roof) < .04 &&
                              std::abs(state->linear_velocity[1]) < .03,
                          "Contour Gallery actor missed its ring, void floor, island or gap: " +
                              std::string(name) + " " + Json(state->position).dump());
            }
            samples.push_back(std::move(sample));
        }
    }
    const auto frame = extract_render_scene(world.simulation.presentation(1));
    check(frame.diagnostics.empty() && frame.cameras.size() == 1 && frame.sprites.size() == 4 &&
              frame.tile_maps.size() == 1,
          "Contour Gallery lost its Camera/Sprite/Map composition");
    return samples;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_gallery_scene::prepare(argv[1], argv[2], argv[3], source);
        const auto authored = playback(prepared.root, prepared.scene);
        const auto output = prepared.root.parent_path() / "Content";
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / "Relocated content";
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        check(playback(moved, prepared.scene) == authored,
              "Source-free relocated contour Gallery playback differs");
        std::ofstream out(prepared.root.parent_path() / "contour-playback.json");
        out << Json{{"scene", prepared.scene},
                    {"samples", authored},
                    {"relocated_equal", true},
                    {"scope",
                     "Native CPU gameplay/content; no Windows or graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        check(bool(out), "Contour Gallery receipt write failed");
        std::cout << "Ring/void/island/disconnected Gallery native queries, four landings and "
                     "exact relocated playback PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
