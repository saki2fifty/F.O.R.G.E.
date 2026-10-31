#include "moving_one_way_scene_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/render_scene.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
using namespace forge;
namespace {
using test_moving_one_way_scene::check;
Json playback(const std::filesystem::path& root, AssetId scene_id) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene_id}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Moving Gallery resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::map<std::string, EntityRef> refs;
    const auto source = world.scene.effective_document();
    for (const auto& row : source.at("entities"))
        refs.emplace(row.at("name").get<std::string>(), world.scene.reference(row.at("id")));
    const auto state = [&](const char* name) {
        const auto value = world.physics()->body_state_2d(refs.at(name));
        check(bool(value), "Moving Gallery lost a native body");
        return *value;
    };
    Json samples = Json::array();
    for (unsigned tick = 0; tick <= 90; ++tick) {
        if (tick)
            world.simulation.tick(1.f / 60);
        if (tick == 30 || tick == 60 || tick == 90) {
            const auto map = state("Moving one-way painted course - edit its translation clip");
            const auto ledge = state("Moving one-way inline ledge - same translation clip");
            const auto amber = state("Amber box - falling onto the inline one-way ledge");
            const auto cyan = state("Cyan circle - rising through both moving one-way surfaces");
            check(map.motion == 1 && ledge.motion == 1 &&
                      std::abs(map.position[0] + 3 - tick * double(1.f / 60) * 3) < .001 &&
                      std::abs(ledge.position[0] - map.position[0]) < .001 &&
                      std::abs(ledge.position[1] - map.position[1] - 4) < .001,
                  "Shared Clip/parent hierarchy did not drive both Kinematic one-way bodies");
            samples.push_back({{"tick", tick},
                               {"map", map.position},
                               {"ledge", ledge.position},
                               {"amber", amber.position},
                               {"cyan", cyan.position}});
            if (tick == 90) {
                const auto amber_id = refs.at("Amber box - falling onto the inline one-way ledge");
                const auto box = world.scene.entity(amber_id.entity.str()).get<BoxCollider2D>();
                if (!(std::abs(amber.position[1] - ledge.position[1] - .1 - .5 * box.y) < .04 &&
                      std::abs(amber.linear_velocity[1]) < .03 && cyan.position[1] > 2.8))
                    throw std::runtime_error("Gallery landing/passage: " +
                                             Json{{"amber", amber.position},
                                                  {"amber_velocity", amber.linear_velocity},
                                                  {"ledge", ledge.position},
                                                  {"box_height", box.y},
                                                  {"cyan", cyan.position},
                                                  {"samples", samples}}
                                                 .dump());
            }
        }
    }
    const auto frame = extract_render_scene(world.simulation.presentation(1));
    check(frame.diagnostics.empty() && frame.cameras.size() == 1 && frame.sprites.size() == 5 &&
              frame.tile_maps.size() == 1,
          "Moving Gallery lost its Camera/Sprite/Map composition");
    return samples;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_moving_one_way_scene::prepare(argv[1], argv[2]);
        test_moving_one_way_scene::settle(prepared, argv[3]);
        const auto authored = playback(prepared.root, prepared.scene);
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto relocated = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported = playback(relocated, prepared.scene);
        check(authored == exported, "Source-free moving one-way playback changed after relocation");
        std::ofstream receipt(prepared.root.parent_path() /
                              (prepared.scene.str() + "-playback.json"));
        receipt << Json{{"scene", prepared.scene},
                        {"samples", authored},
                        {"relocated_equal", true},
                        {"scope", "Actual native CPU source-free gameplay/content; no Windows or "
                                  "graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Moving Gallery playback receipt write failed");
        std::cout << "Moving one-way Gallery: shared Clip/hierarchy, landing/passage and exact "
                     "source-free relocated native playback PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
