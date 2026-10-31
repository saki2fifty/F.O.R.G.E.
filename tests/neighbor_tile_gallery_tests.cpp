#include "neighbor_tile_scene_tests.hpp"
#include "runtime_package.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    using namespace forge;
    using test_neighbor_tile_scene::check;
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_gallery_scene::prepare(argv[1], argv[2], argv[3],
                                                               test_neighbor_tile_scene::source);
        const auto authored = test_neighbor_tile_scene::playback(prepared.root, prepared.scene);
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        check(test_neighbor_tile_scene::playback(moved, prepared.scene) == authored,
              "Source-free relocated neighbor Gallery playback differs");
        std::ofstream out(prepared.root.parent_path() / "neighbor-playback.json");
        out << Json{{"scene", prepared.scene},
                    {"samples", authored},
                    {"relocated_equal", true},
                    {"scope",
                     "Native CPU gameplay/content; no Windows or graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        check(bool(out), "Animated Gallery playback receipt write failed");
        std::cout << "Neighbor edges/hole/flipped/animated Gallery and relocated playback PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
