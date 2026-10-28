#pragma once
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include "sprite_fill_gallery_render_tests.hpp"
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <thread>

namespace forge::test_sprite_gallery_render {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& root,
              const std::filesystem::path& images) {
    using test_sprite_draw::check;
    std::filesystem::create_directories(images);
    const auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    const auto find = [&](const char* path) {
        // Runtime packages deliberately rewrite source paths. These immutable
        // curated fixture IDs exercise the same logical AssetRef in both roots.
        constexpr std::pair<const char*, const char*> fixtures[]{
            {"Scenes/71-world-sprites.scene.json", "f436af02-5e7d-4212-84a8-361768617acf"},
            {"Scenes/72-sprite-frame-animation.scene.json", "481ba836-7831-4266-a980-8f27abf8fc47"},
            {"Scenes/78-sprite-image-regions.scene.json", "e73043a0-eb10-4d5f-9375-640773df0bbe"},
            {"Scenes/87-sprite-frame-regions.scene.json", "15400000-1540-4154-8154-154000000000"},
            {"Scenes/95-sprite-frame-metadata.scene.json", "4660b557-cf48-4bc6-896d-ce536722d079"},
            {"Scenes/97-packed-sprite-fills.scene.json", "2acc279b-0a76-4838-a1bc-b70123cb234a"}};
        for (const auto& [source, identity] : fixtures)
            if (std::string_view(path) == source) {
                const auto id = AssetId::parse(identity);
                const auto record = catalog->records().find(id);
                check(record != catalog->records().end() && record->second.type == SceneAsset::type,
                      "Native Sprite Gallery Scene identity is missing");
                return id;
            }
        throw std::runtime_error("Unknown native Sprite Gallery fixture");
    };
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(catalog);
    FrameRenderer game(presentation);
    game.resources(host);
    auto* device = presentation.device();
    for (const auto source :
         {"Scenes/71-world-sprites.scene.json", "Scenes/72-sprite-frame-animation.scene.json",
          "Scenes/78-sprite-image-regions.scene.json", "Scenes/87-sprite-frame-regions.scene.json",
          "Scenes/95-sprite-frame-metadata.scene.json"}) {
        const bool metadata = std::string_view(source).starts_with("Scenes/95");
        const bool bank = std::string_view(source).starts_with("Scenes/87");
        const bool regions = bank || std::string_view(source).starts_with("Scenes/78");
        const bool animated = regions || std::string_view(source).starts_with("Scenes/72");
        EngineContext engine(WorldRole::Runtime, false, {animation_module(root)});
        Scene scene(engine.world());
        scene.restore_snapshot(load_game_scene(root, {find(source)}));
        const auto animation = animation_runtime(engine.world());
        animation->catalog(catalog);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Native Sprite Gallery clip timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        RuntimeSimulation simulation(engine.world(), scene);
        simulation.reset_presentation();
        for (unsigned frame = 0; frame < (animated ? 4u : 1u); ++frame) {
            if (frame)
                simulation.tick(.5f);
            const auto document = simulation.presentation(1);
            for (const auto& size : {std::pair{640u, 360u}, {360u, 640u}}) {
                const auto [width, height] = size;
                test_sprite_draw::Pixels pixels;
                const auto ready = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                do {
                    presentation.begin_frame();
                    auto* output = game.game(context, document, width, height);
                    presentation.end_frame(context);
                    host->submit();
                    pixels = readback(output->GetTexture());
                    context->FinishFrame();
                    device->ReleaseStaleResources();
                    check(std::chrono::steady_clock::now() < ready,
                          "Native Gallery images did not settle");
                } while (game.pending());
                check(game.diagnostics().empty() && game.game_scene() &&
                          game.game_scene()->sprites.size() == (metadata               ? 5u
                                                                : regions || !animated ? 6u
                                                                                       : 5u),
                      "Actual Gallery image/material/Game consumer failed");
                const auto sample = [&](double x, double y) {
                    const auto vertical = metadata ? 9. : regions ? 10. : 8.;
                    const auto px = unsigned(std::floor(width * .5 + x * height / vertical));
                    const auto py = unsigned(std::floor(height * .5 - y * height / vertical));
                    check(px < width && py < height,
                          "Gallery sample escaped its orthographic view");
                    return pixels[std::size_t(py) * width + px];
                };
                if (metadata) {
                    for (const auto x : {-2.4, 2.4}) {
                        const auto arrow = sample(x, 1.6), marker = sample(x, -1.1);
                        check(arrow[0] > arrow[1] && arrow[1] > arrow[2] && marker[1] > marker[0] &&
                                  marker[2] > marker[0],
                              "Original/trimmed/rotated Gallery canvases did not reach actual Game "
                              "pixels");
                    }
                } else if (animated) {
                    const auto p = sample(0, regions ? 0 : 1.1);
                    check(p[3] == 255 && (frame == 0   ? p[1] > p[2] && p[2] > p[0]
                                          : frame == 1 ? p[2] > p[1] && p[1] > p[0]
                                          : frame == 2 ? p[0] > p[1] && p[1] > p[2]
                                                       : p[0] > p[2] && p[2] > p[1]),
                          "Published frame animation did not reach actual Game HDR/display pixels");
                } else {
                    const auto gold = sample(-.7, 1.5), green = sample(-1, -2);
                    const auto overlap = sample(2, 1.5);
                    check(gold[0] > gold[1] && gold[1] > gold[2] && green[1] > green[0] &&
                              green[1] > green[2] && overlap[2] > overlap[0] &&
                              overlap[0] > overlap[1],
                          "Original Gallery image/tint/overlap did not reach Game pixels");
                    if (width > height) {
                        const auto cyan = sample(-3, 1.5);
                        check(cyan[2] > cyan[1] && cyan[1] > cyan[0],
                              "Wide Gallery view lost the cyan image");
                    }
                }
                const auto name = std::string(metadata   ? "gallery95-frame"
                                              : bank     ? "gallery87-frame"
                                              : regions  ? "gallery78-frame"
                                              : animated ? "gallery72-frame"
                                                         : "gallery71-frame") +
                                  std::to_string(frame) +
                                  (width > height ? "-wide.ppm" : "-tall.ppm");
                std::ofstream file(images / name, std::ios::binary);
                file << "P6\n" << width << ' ' << height << "\n255\n";
                for (const auto pixel : pixels)
                    file.write(reinterpret_cast<const char*>(pixel.data()), 3);
                check(bool(file), "Native Gallery capture write failed");
            }
        }
    }
    test_sprite_fill_gallery::exercise(presentation, context, readback, root, images,
                                       find("Scenes/97-packed-sprite-fills.scene.json"));
}
} // namespace forge::test_sprite_gallery_render
