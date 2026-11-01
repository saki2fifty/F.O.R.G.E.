#include "runtime_package.hpp"
#include "sprite_atlas_gallery_fixture.hpp"
#include "sprite_render_values.hpp"
#include <iostream>

using namespace forge;
namespace {
Json playback(const std::filesystem::path& root) {
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {test_sprite_atlas_gallery::scene_id()}));
    test_sprite_atlas_gallery::ready(world);
    const auto catalog = AssetCatalog::open_project(root);
    const auto texture = asset_detail::load_texture_selection(
        root, catalog, {test_sprite_atlas_gallery::atlas_id()});
    test_sprite_atlas_gallery::check(texture.value->sprite_frames.size() == 3,
                                     "Gallery atlas pixels lost their attached frame table");
    Json samples = Json::array();
    for (unsigned tick = 0; tick <= 195; ++tick) {
        if (tick)
            world.simulation.tick(1.f / 60);
        if (tick % 60 == 15) {
            const auto frame = test_sprite_atlas_gallery::sample(world, tick);
            Json values = Json::array();
            for (const auto& sprite : frame.sprites) {
                const auto draw = sprite_draw_values(sprite.renderer, *texture.value);
                values.push_back({{"entity", sprite.entity},
                                  {"key", sprite.renderer.frame},
                                  {"mip", sprite.renderer.mip_filter},
                                  {"origin", draw.region.origin},
                                  {"scale", draw.region.scale},
                                  {"crop", draw.quad_crop},
                                  {"turn", draw.transpose_uv}});
            }
            samples.push_back({{"tick", tick}, {"sprites", std::move(values)}});
        }
    }
    return samples;
}
} // namespace
int main(int argc, char** argv) {
    try {
        test_sprite_atlas_gallery::check(argc == 4, "Need Gallery, scratch and matching worker");
        const auto root = test_sprite_atlas_gallery::prepare(argv[1], argv[2], argv[3]);
        const auto authored = playback(root);
        const std::array roots{test_sprite_atlas_gallery::scene_id()};
        const auto target = desktop_texture_target();
        const auto output = root.parent_path() / "Content";
        package_runtime_content(root, output, roots, {target.platform, target.backend});
        const auto relocated = root.parent_path() / "Relocated Atlas Game";
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(root, root.string() + "-source-offline");
        const auto exported_catalog = AssetCatalog::open_project(relocated);
        for (const auto& [id, record] : exported_catalog.records())
            test_sprite_atlas_gallery::check(
                id == test_sprite_atlas_gallery::scene_id() ||
                    id == test_sprite_atlas_gallery::atlas_id() ||
                    id == test_sprite_atlas_gallery::material_id() ||
                    id == test_sprite_atlas_gallery::clip_id(),
                "Atlas build-only input leaked into the runtime package");
        test_sprite_atlas_gallery::check(
            authored == playback(relocated),
            "Source-free relocated atlas animation/pixel mapping differs");
        std::cout << "Gallery127 source-free relocated sparse keys, all mip modes and held "
                     "animation passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
