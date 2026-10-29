#pragma once
#include "frame_renderer.hpp"
#include "runtime_tile_map.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <fstream>
#include <thread>

namespace forge::test_tile_gallery_render {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              const Readback& readback, const std::filesystem::path& root,
              const std::filesystem::path& images, bool polygons = false) {
    using test_sprite_draw::check;
    std::filesystem::create_directories(images);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    AssetId scene_id;
    for (const auto& [id, record] : catalog->records())
        if (record.source == (polygons ? "Scenes/101-tile-collision-profiles.scene.json"
                                       : "Scenes/74-tile-maps.scene.json") &&
            !record.subasset)
            scene_id = id;
    check(bool(scene_id), "Tile Gallery scene missing");
    EngineContext engine(WorldRole::Runtime, false, {runtime_resources_module(root)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(root, {scene_id}));
    RuntimeSimulation simulation(engine.world(), scene);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(catalog);
    FrameRenderer game(presentation);
    game.resources(host);
    auto* device = presentation.device();
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{1280u, 720u}}) {
        test_sprite_draw::Pixels pixels;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do {
            auto frame = extract_render_scene(simulation.presentation(1));
            detail::bind_runtime_tile_maps(engine.world(), frame);
            presentation.begin_frame();
            auto* output = game.game(context, std::move(frame), width, height);
            presentation.end_frame(context);
            host->submit();
            pixels = readback(output->GetTexture());
            context->FinishFrame();
            device->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < end, "Native Tile Gallery did not settle");
            if (game.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (game.pending());
        std::ofstream file(
            images / ((polygons ? "gallery101-" : "gallery74-") + std::to_string(width) + ".ppm"),
            std::ios::binary);
        file << "P6\n" << width << ' ' << height << "\n255\n";
        for (const auto pixel : pixels)
            file.write(reinterpret_cast<const char*>(pixel.data()), 3);
        check(bool(file), "Tile Gallery capture write failed");
        check(game.diagnostics().empty() && game.game_scene() &&
                  game.game_scene()->tile_maps.size() == (polygons ? 1u : 2u) &&
                  game.game_scene()->cameras.size() == 1,
              "Actual Tile Gallery Game renderer lost maps or camera");
        for (const auto& map : game.game_scene()->tile_maps) {
            check(map.runtime_instance && map.cells &&
                      map.cells->occupied() == (polygons ? 23u : 40u),
                  "Gallery Game renderer used authored fallback or lost cells");
            if (polygons) {
                const auto sample = [&](double x, double y) {
                    const auto px = unsigned(width * .5 + x * height / 14.);
                    const auto py = unsigned(height * .5 - (y - 1) * height / 14.);
                    check(px < width && py < height, "Collision Gallery sample left the viewport");
                    return pixels.at(py * width + px);
                };
                for (double x : {-4.25, 4.25}) {
                    const auto color = sample(x, -2.75);
                    check(color[0] > color[1] + 20 && color[1] > color[2] + 40,
                          "Original/mirrored convex ramp lost its amber silhouette");
                }
                const auto background = sample(-10, 5);
                for (double x : {-4.75, 4.75}) {
                    const auto color = sample(x, -2.25);
                    bool matches = true;
                    for (unsigned axis = 0; axis < 4; ++axis)
                        matches &= std::abs(int(color[axis]) - int(background[axis])) <= 1;
                    const auto message = "Ramp alpha differs from the rendered clear background: " +
                                         nlohmann::json(color).dump() + " versus " +
                                         nlohmann::json(background).dump();
                    check(matches, message.c_str());
                }
                const auto color = sample(6.5, -3.5);
                check(color[1] > color[0] + 40 && color[2] > color[0] + 40,
                      "Collision Gallery lost its full-cell cyan floor");
            } else {
                // Independently project the centers of two known original image cells.
                // Camera is at (0,0,-12), +Z forward, orthographic height 12.
                for (const int cell_x : {-6, -5}) {
                    const double x = map.world.m[0] * (cell_x + .5) * .75 + map.world.m[3];
                    const double y = map.world.m[5] * 2.5 * .75 + map.world.m[7];
                    const auto px = unsigned(width * .5 + x * height / 12.);
                    const auto py = unsigned(height * .5 - y * height / 12.);
                    check(px < width && py < height, "Gallery sample center left the viewport");
                    const auto color = pixels.at(py * width + px);
                    if (cell_x == -6)
                        check(color[1] > color[0] + 40 && color[1] > color[2] + 40,
                              "Layered/mirrored Gallery map lost the green corner marker");
                    else
                        check(color[2] > color[0] + 40 && color[2] > color[1],
                              "Layered/mirrored Gallery map lost the blue right arrow");
                }
            }
        }
    }
}
} // namespace forge::test_tile_gallery_render
