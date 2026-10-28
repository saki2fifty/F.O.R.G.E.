#pragma once
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <thread>

namespace forge::test_fog_gallery {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& root,
              const std::filesystem::path& images) {
    using test_sprite_draw::check;
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(catalog);
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(
        load_game_scene(root, {AssetId::parse("da66d15d-42ed-4342-94ce-19d67de0e105")}));
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    auto extracted = extract_render_scene(simulation.presentation(1));
    FrameRenderer game(presentation);
    game.resources(host);
    std::filesystem::create_directories(images);
    for (const auto& size : {std::pair{640u, 360u}, {360u, 640u}}) {
        const auto [width, height] = size;
        const auto render = [&](const RenderScene& source) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            test_sprite_draw::Pixels pixels;
            do {
                presentation.begin_frame();
                auto* output = game.game(context, source, width, height);
                presentation.end_frame(context);
                host->submit();
                pixels = readback(output->GetTexture());
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < deadline, "Fog Gallery resources stalled");
            } while (game.pending());
            if (!game.diagnostics().empty())
                throw std::runtime_error("Fog Gallery: " + game.diagnostics().front().text);
            return pixels;
        };
        const auto fogged = render(extracted);
        auto clear = extracted;
        clear.settings.distance_fog.enabled = false;
        check(render(clear) != fogged, "Distance fog did not change the composed Gallery image");
        check(render(extracted) == fogged, "Fog toggle retained stale draw values");
        // Radial distance is relative to each camera, including layered views.
        auto layered = extracted;
        auto second = layered.cameras.front();
        second.entity = EntityId::generate();
        second.camera.order = 1;
        second.camera.viewport_x = .5;
        second.camera.viewport_width = .5;
        second.world.m[11] += 10;
        layered.cameras.front().camera.viewport_width = .5;
        layered.cameras.push_back(second);
        check(render(layered) != fogged && render(extracted) == fogged,
              "Multiple fog cameras leaked viewport or camera-relative state");
        const auto path = images / ("distance-fog-" + std::to_string(width) + "x" +
                                    std::to_string(height) + ".ppm");
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << width << ' ' << height << "\n255\n";
        for (const auto& pixel : fogged)
            file.write(reinterpret_cast<const char*>(pixel.data()), 3);
        check(bool(file.flush()), "Fog Game image write failed");
    }
    std::cout << "Gallery98 actual Game fog/toggle/per-camera/wide/tall pixels PASS\n";
}
} // namespace forge::test_fog_gallery
