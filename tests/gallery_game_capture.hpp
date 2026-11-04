#pragma once
#include "frame_renderer.hpp"
#include "runtime_tile_map.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <set>
#include <thread>

namespace forge::test_gallery_capture {
using namespace Diligent;
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
using Pixels = std::vector<std::array<std::uint8_t, 4>>;
inline void ppm(const std::filesystem::path& path, const Pixels& pixels, unsigned width,
                unsigned height) {
    std::ofstream stream(path, std::ios::binary);
    stream << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& value : pixels)
        stream.write(reinterpret_cast<const char*>(value.data()), 3);
    check(bool(stream), "Gallery image write failed");
}
inline std::vector<Pixels> render(DiligentPresentation& presentation, IDeviceContext* context,
                                  const std::filesystem::path& root, AssetId scene_id,
                                  const std::filesystem::path& output,
                                  std::size_t minimum_colors = 21, bool expect_animation = true,
                                  std::array<unsigned, 3> sample_ticks = {30, 60, 90},
                                  bool project_simulation_time = false) {
    check(sample_ticks[0] > 0 && sample_ticks[0] < sample_ticks[1] &&
              sample_ticks[1] < sample_ticks[2] && sample_ticks[2] <= 3600,
          "Gallery sample ticks must increase within the bounded capture interval");
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene_id}));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Gallery native assets stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer frame(presentation);
    frame.resources(host);
    std::filesystem::create_directories(output);
    std::vector<Pixels> results;
    const auto capture = [&](unsigned tick, unsigned width, unsigned height) {
        // This fixture advances exactly one 1/60 tick per loop. Opted-in
        // procedural geometry receives that explicit simulation sample too.
        auto scene = extract_render_scene(project_simulation_time
                                              ? world.simulation.presentation(1, double(tick) / 60)
                                              : world.simulation.presentation(1));
        detail::bind_runtime_tile_maps(world.engine.world(), scene);
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* image = frame.game(context, scene, width, height);
            check(image, "Gallery has no Game display");
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline, "Gallery visual assets stalled");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        if (!frame.diagnostics().empty())
            throw std::runtime_error(frame.diagnostics().front().text);
        check(frame.omitted_diagnostics() == 0 && pixels.size() == std::size_t(width) * height,
              "Gallery display is incomplete");
        std::set<std::array<std::uint8_t, 4>> colors(pixels.begin(), pixels.end());
        ppm(output / (std::to_string(tick) + "-" + std::to_string(width) + ".ppm"), pixels, width,
            height);
        check(colors.size() >= minimum_colors,
              "Gallery did not render its expected course and actor colors");
        results.push_back(std::move(pixels));
    };
    for (unsigned tick = 1; tick <= sample_ticks.back(); ++tick) {
        world.simulation.tick(1.f / 60);
        if (std::find(sample_ticks.begin(), sample_ticks.end(), tick) != sample_ticks.end())
            capture(tick, 640, 360);
    }
    capture(sample_ticks.back(), 360, 640);
    unsigned changed = 0;
    for (std::size_t i = 0; i < results[0].size(); ++i)
        changed += results[0][i] != results[2][i];
    check(expect_animation ? changed > 100 : changed == 0,
          expect_animation ? "Gallery did not visibly animate its gameplay"
                           : "Static Gallery unexpectedly changed during gameplay");
    return results;
}
} // namespace forge::test_gallery_capture
