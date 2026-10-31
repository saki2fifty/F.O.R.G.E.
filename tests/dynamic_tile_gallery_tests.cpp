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
constexpr auto source = "Scenes/124-dynamic-tile-map-bodies.scene.json";
Json playback(const std::filesystem::path& root, AssetId scene) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Dynamic Map Gallery assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::map<std::string, EntityRef> maps;
    const auto document = world.scene.document();
    for (const auto& row : document.at("entities"))
        if (row.at("components").contains("forge.tile_map_renderer"))
            maps.emplace(row.at("name").get<std::string>(),
                         world.scene.reference(row.at("id").get<std::string>()));
    check(maps.size() == 3 && world.physics()->status().at("bodies_2d") == 4 &&
              world.physics()->status().at("shapes_2d") == 43,
          "Dynamic Map Gallery lost one-body compounds or contour geometry");
    std::map<std::string, double> angles;
    std::map<std::string, std::uint64_t> epochs;
    std::map<std::string, std::shared_ptr<const detail::RuntimeTileMapSnapshot>> cells;
    for (const auto& [name, ref] : maps) {
        const auto state = world.physics()->body_state_2d(ref);
        const double mass = name.starts_with("Mirrored") ? 12 : 2.953125;
        check(state && std::abs(state->mass - mass) < 1e-5,
              "Dynamic Map Gallery density/override mass differs");
        angles.emplace(name, state->angle);
        epochs.emplace(name, state->epoch);
        cells.emplace(name, detail::runtime_tile_map_snapshot(world.engine.world(), ref));
        check(cells.at(name) && cells.at(name)->cells.occupied() == 2 &&
                  cells.at(name)->definition_values.tile(2).collision.contours,
              "Dynamic Map Gallery lost its two logical cells or source6 profiles");
    }
    Json samples = Json::array();
    for (unsigned tick = 1; tick <= 240; ++tick) {
        world.simulation.tick(1.f / 60);
        if (tick != 30 && tick != 90 && tick != 240)
            continue;
        Json sample{{"tick", tick}};
        for (const auto& [name, ref] : maps) {
            const auto state = world.physics()->body_state_2d(ref);
            check(state && state->epoch == epochs.at(name) &&
                      detail::runtime_tile_map_snapshot(world.engine.world(), ref) ==
                          cells.at(name),
                  "Dynamic Map Gallery motion rebuilt cells or changed native epoch");
            sample[name] = {{"position", state->position},
                            {"angle", state->angle},
                            {"velocity", state->linear_velocity}};
            if (tick == 30)
                check(state->position[1] < 1.5 && state->linear_velocity[1] < -1,
                      "Dynamic Map Gallery did not fall under Project gravity");
            if (tick == 240) {
                check(state->position[1] > -4 && state->position[1] < 1 &&
                          std::hypot(state->linear_velocity[0], state->linear_velocity[1]) < .12,
                      "Dynamic Map Gallery missed its Static receiver: " + name + " " +
                          Json(state->position).dump());
                check(name.starts_with("Rotation locked")
                          ? std::abs(state->angle) < 1e-6
                          : std::abs(state->angle - angles.at(name)) > .05,
                      "Dynamic Map Gallery lost tumbling or its rotation lock: " + name);
            }
        }
        samples.push_back(std::move(sample));
    }
    const auto frame = extract_render_scene(world.simulation.presentation(1));
    check(frame.diagnostics.empty() && frame.cameras.size() == 1 && frame.sprites.size() == 1 &&
              frame.tile_maps.size() == 3,
          "Dynamic Map Gallery lost Camera/receiver/Map presentation");
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
              "Source-free relocated Dynamic Map Gallery playback differs");
        std::ofstream out(prepared.root.parent_path() / "dynamic-map-playback.json");
        out << Json{{"scene", prepared.scene},
                    {"samples", authored},
                    {"relocated_equal", true},
                    {"scope",
                     "Native CPU gameplay/content; no Windows or graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        check(bool(out), "Dynamic Map Gallery receipt write failed");
        std::cout << "Dynamic Map Gallery fall/tumble/mirror/mass/rotation lock and exact "
                     "source-free relocated playback PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
