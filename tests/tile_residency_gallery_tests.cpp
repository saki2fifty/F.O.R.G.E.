#include "runtime_package.hpp"
#include "tile_residency_gallery_fixture.hpp"
#include <forge/game_content.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
Json playback(const std::filesystem::path& root, AssetId scene) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    test_tile_residency::ready(world);
    Json samples = Json::array();
    for (unsigned tick = 0; tick <= 1200; ++tick) {
        if (tick)
            world.simulation.tick(1.f / 60);
        if (tick % 240 == 0) {
            const auto frame = test_tile_residency::sample(world, tick);
            samples.push_back({{"tick", tick},
                               {"camera_x", frame.cameras.front().world.m[3]},
                               {"cells", frame.tile_maps.front().cells->occupied()}});
        }
    }
    return samples;
}
} // namespace
int main(int argc, char** argv) {
    try {
        test_tile_residency::check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_residency::prepare(argv[1], argv[2], argv[3]);
        const auto authored = playback(prepared.root, prepared.scene);
        const std::array roots{prepared.scene};
        const auto output = prepared.root.parent_path() / "Content";
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto relocated = prepared.root.parent_path() / "Relocated Game Content";
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported = playback(relocated, prepared.scene);
        test_tile_residency::check(
            authored == exported,
            "Source-free residency Gallery camera/cells changed after relocation");
        std::ofstream receipt(prepared.root.parent_path() / "playback.json");
        receipt << Json{{"scene", prepared.scene},
                        {"samples", authored},
                        {"relocated_equal", true},
                        {"scope",
                         "Native CPU runtime/content; no Windows or graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        test_tile_residency::check(bool(receipt), "Residency Gallery playback receipt failed");
        std::cout
            << "Residency Gallery: six camera samples and exact relocated source-free cells PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
