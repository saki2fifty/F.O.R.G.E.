#pragma once
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <fstream>

namespace forge::test_sprite_fill_gallery {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& root,
              const std::filesystem::path& images, AssetId scene_id) {
    using test_sprite_draw::check;
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(catalog);
    FrameRenderer game(presentation), reference(presentation);
    game.resources(host);
    reference.resources(host);
    EngineContext engine(WorldRole::Runtime, false);
    Scene hydrated(engine.world());
    hydrated.restore_snapshot(load_game_scene(root, {scene_id}));
    RuntimeSimulation simulation(engine.world(), hydrated);
    simulation.reset_presentation();
    const auto scene = extract_render_scene(simulation.presentation(1));
    if (scene.sprites.size() != 4 || scene.cameras.size() != 1 || !scene.diagnostics.empty()) {
        std::string message =
            "Packed-fill Gallery admission: sprites=" + std::to_string(scene.sprites.size()) +
            " cameras=" + std::to_string(scene.cameras.size());
        for (const auto& error : scene.diagnostics)
            message += " " + error.category + ": " + error.text;
        throw std::runtime_error(message);
    }
    auto originals = scene;
    for (auto& sprite : originals.sprites) {
        if (sprite.world.m[3] < 0)
            continue;
        const auto original =
            std::find_if(scene.sprites.begin(), scene.sprites.end(), [&](const auto& row) {
                return row.world.m[3] < 0 && row.renderer.image_mode == sprite.renderer.image_mode;
            });
        check(original != scene.sprites.end() && !sprite.renderer.frame_regions.empty(),
              "Gallery packed/original fill pairing is incomplete");
        sprite.renderer.material = original->renderer.material;
        sprite.renderer.frame_regions.clear();
        sprite.renderer.frame = 0;
    }
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        const auto render = [&](FrameRenderer& renderer, const RenderScene& source) {
            test_sprite_draw::Pixels pixels;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            do {
                presentation.begin_frame();
                auto* output = renderer.game(context, source, width, height);
                presentation.end_frame(context);
                host->submit();
                pixels = readback(output->GetTexture());
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < deadline,
                      "Packed-fill Gallery resource admission stalled");
            } while (renderer.pending());
            check(renderer.diagnostics().empty(), "Packed-fill Gallery has rendering diagnostics");
            return pixels;
        };
        const auto actual = render(game, scene), expected = render(reference, originals);
        check(actual.size() == expected.size(), "Gallery fill changed the display extent");
        unsigned visible = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            visible += actual[i][0] > actual[i][2] + 20 || actual[i][1] > actual[i][0] + 20;
            for (unsigned lane = 0; lane < 4; ++lane)
                check(std::abs(int(actual[i][lane]) - int(expected[i][lane])) <= 2,
                      "Actual Gallery97 packed fills differ from their original-image reference");
        }
        check(visible > 100, "Gallery97 image comparison contains no visible artwork");
        std::filesystem::create_directories(images);
        const auto name = width > height ? "gallery97-wide.ppm" : "gallery97-tall.ppm";
        std::ofstream file(images / name, std::ios::binary);
        file << "P6\n" << width << ' ' << height << "\n255\n";
        for (const auto& pixel : actual)
            file.write(reinterpret_cast<const char*>(pixel.data()), 3);
        check(bool(file), "Gallery97 native capture write failed");
    }
}
} // namespace forge::test_sprite_fill_gallery
