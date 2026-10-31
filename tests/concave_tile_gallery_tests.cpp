#include "concave_tile_scene_tests.hpp"
#include "runtime_package.hpp"
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <thread>
using namespace forge;
namespace {
using test_concave_tile_scene::check;
Json playback(const std::filesystem::path& root, AssetId scene) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Concave Gallery assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::map<std::string, EntityRef> refs;
    const auto document = world.scene.document();
    for (const auto& row : document.at("entities"))
        refs.emplace(row.at("name").get<std::string>(), world.scene.reference(row.at("id")));
    const std::array lanes{std::pair{"Amber - L upper lane", 0.}, std::pair{"Cyan - L notch", -2.},
                           std::pair{"Rose - U upper lane", 0.},
                           std::pair{"Green - U deep notch", -3.}};
    Json samples = Json::array();
    for (unsigned tick = 0; tick <= 180; ++tick) {
        if (tick)
            world.simulation.tick(1.f / 60);
        if (tick == 30 || tick == 90 || tick == 180) {
            Json sample{{"tick", tick}};
            for (const auto& [name, roof] : lanes) {
                const auto ref = refs.at(name);
                const auto state = world.physics()->body_state_2d(ref);
                check(bool(state), "Concave Gallery actor has no native body");
                sample[name] = state->position;
                if (tick == 180) {
                    const auto entity = world.scene.entity(ref.entity.str());
                    const auto half = entity.has<CircleCollider2D>()
                                          ? entity.get<CircleCollider2D>().radius
                                          : .5 * entity.get<BoxCollider2D>().y;
                    check(std::abs(state->position[1] - half - roof) < .04 &&
                              std::abs(state->linear_velocity[1]) < .03,
                          "Concave Gallery actor missed its upper edge or notch: " +
                              std::string(name) + " " + Json(state->position).dump());
                }
            }
            samples.push_back(std::move(sample));
        }
    }
    const auto frame = extract_render_scene(world.simulation.presentation(1));
    check(frame.diagnostics.empty() && frame.cameras.size() == 1 && frame.sprites.size() == 4 &&
              frame.tile_maps.size() == 1,
          "Concave Gallery lost its Camera/Sprite/Map composition");
    return samples;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_concave_tile_scene::prepare(argv[1], argv[2], argv[3]);
        const auto authored = playback(prepared.root, prepared.scene);
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        check(playback(moved, prepared.scene) == authored,
              "Source-free relocated concave Gallery playback differs");
        std::ofstream out(prepared.root.parent_path() / "concave-playback.json");
        out << Json{{"scene", prepared.scene},
                    {"samples", authored},
                    {"relocated_equal", true},
                    {"scope",
                     "Native CPU gameplay/content; no Windows or graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        check(bool(out), "Concave Gallery receipt write failed");
        std::cout << "Concave L/U Gallery upper/notch landing and exact relocated playback PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
