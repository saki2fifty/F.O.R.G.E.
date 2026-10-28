#pragma once
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <chrono>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <fstream>

namespace forge::test_sprite_mip_gallery {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& root,
              const std::filesystem::path& images, AssetId scene_id) {
    using test_sprite_draw::check;
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer game(presentation);
    game.resources(host);
    EngineContext engine(WorldRole::Runtime, false);
    Scene hydrated(engine.world());
    hydrated.restore_snapshot(load_game_scene(root, {scene_id}));
    RuntimeSimulation simulation(engine.world(), hydrated);
    simulation.reset_presentation();
    const auto scene = extract_render_scene(simulation.presentation(1));
    check(scene.sprites.size() == 12 && scene.cameras.size() == 1 && scene.diagnostics.empty(),
          "Frame-filtering Gallery did not load its complete authored scene");
    std::filesystem::create_directories(images);
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        test_sprite_draw::Pixels pixels;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            presentation.begin_frame();
            auto* output = game.game(context, scene, width, height);
            presentation.end_frame(context);
            host->submit();
            pixels = readback(output->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline,
                  "Frame-filtering Gallery resource admission stalled");
        } while (game.pending());
        check(game.diagnostics().empty(), "Frame-filtering Gallery has render diagnostics");
        const auto cameras = prepare_game_cameras(scene, width, height);
        check(cameras.cameras.size() == 1 && cameras.diagnostics.empty(),
              "Frame-filtering Gallery camera composition is invalid");
        const auto& area = cameras.cameras[0].view.viewport;
        const auto sample = [&](double x, double y) {
            const auto px = unsigned(area.x + area.width * .5 + x * area.height / 8.);
            const auto py = unsigned(area.y + area.height * .5 - y * area.height / 8.);
            check(px < width && py < height, "Frame-filtering Gallery sample escaped the camera");
            return pixels[std::size_t(py) * width + px];
        };
        for (const auto x : {0., 3.}) {
            const auto checks = sample(x, 2.7), stripes = sample(x, .9);
            const auto transparent = sample(x, -.9), ring = sample(x, -2.7);
            check(checks[0] > 60 && std::abs(int(checks[0]) - int(checks[2])) <= 2 &&
                      checks[1] < 5 && stripes[1] > stripes[0] + 10 &&
                      std::abs(int(stripes[0]) - int(stripes[2])) <= 2 &&
                      transparent[0] > transparent[1] + 50 &&
                      transparent[0] > transparent[2] + 50 && ring[2] > ring[0] + 50,
                  "Actual filtered Gallery colors mixed neighbors, invisible RGB or frame layout");
        }
        check(host->inspect().gpu_textures.resident == 5,
              "Gallery did not share four derived frames across both filter profiles");
        const auto name = width > height ? "gallery99-wide.ppm" : "gallery99-tall.ppm";
        std::ofstream file(images / name, std::ios::binary);
        file << "P6\n" << width << ' ' << height << "\n255\n";
        for (const auto& pixel : pixels)
            file.write(reinterpret_cast<const char*>(pixel.data()), 3);
        check(bool(file), "Frame-filtering Gallery capture failed");
        std::cout << "Gallery99 " << width << 'x' << height << " GPU texture payload "
                  << host->inspect().gpu_textures.payload_bytes << " bytes\n";
    }
}
} // namespace forge::test_sprite_mip_gallery
