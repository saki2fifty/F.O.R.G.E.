#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include "sprite_atlas_gallery_fixture.hpp"
#include "sprite_draw_render_tests.hpp"
#include "view_image.hpp"
#include <fstream>
#include <iostream>

using namespace forge;
using namespace Diligent;
namespace {
using test_sprite_atlas_gallery::check;
auto capture(DiligentPresentation& presentation, IDeviceContext* context,
             const std::filesystem::path& root, const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {test_sprite_atlas_gallery::scene_id()}));
    test_sprite_atlas_gallery::ready(world);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer game(presentation);
    game.resources(host);
    forge::Viewport preview(presentation);
    preview.resources(host);
    EditorCamera orbit;
    orbit.target = {0, 0, 0};
    orbit.distance = 12;
    orbit.projection = CameraProjection::Orthographic;
    orbit.orthographic_height = 8.5;
    std::vector<test_sprite_draw::Pixels> scene_samples, game_samples;
    for (unsigned tick = 0; tick <= 195; ++tick) {
        if (tick)
            world.simulation.tick(1.f / 60);
        if (tick % 60 != 15)
            continue;
        const auto frame = test_sprite_atlas_gallery::sample(world, tick);
        const auto document = world.scene.effective_document();
        test_sprite_draw::Pixels scene_pixels, game_pixels;
        const auto scene_render = [&] {
            presentation.begin_frame();
            auto* texture =
                preview.render(context, document, 640, 360, orbit, tick + 1, false, {false});
            presentation.end_frame(context);
            host->submit();
            scene_pixels = test_sprite_draw::read_pixels(presentation.device(), context,
                                                         texture->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
        };
        const auto scene_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do {
            scene_render();
            check(std::chrono::steady_clock::now() < scene_deadline,
                  "Atlas Scene preparation stalled");
            if (preview.meshes()->pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (preview.meshes()->pending());
        check(preview.meshes()->diagnostics().empty(),
              "Atlas Scene has incomplete render resources");
        const auto redraws = preview.redraws;
        scene_render();
        check(preview.redraws == redraws && preview.retained > 0,
              "Ready atlas Scene did not retain its complete image");
        const auto first =
            std::find_if(frame.sprites.begin(), frame.sprites.end(), [](const auto& sprite) {
                return sprite.world.m[3] < -2 && sprite.world.m[7] > 1;
            });
        check(first != frame.sprites.end(), "Atlas Scene left reference Sprite missing");
        const double x = 320 - 3 * 360 / 8.5, y = 180 - 1.8 * 360 / 8.5;
        check(preview.pick(document, orbit, 640, 360, x, y) == first->entity.str(),
              "Atlas Scene picking lost its logical Sprite quad");
        auto framed = orbit;
        check(preview.frame_selection(document, {first->entity.str()}, framed, 640.f / 360) &&
                  std::abs(framed.target[0] + 3) < .0001 &&
                  std::abs(framed.target[1] - 1.8) < .0001,
              "Atlas Scene framing changed the logical canvas anchor");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do {
            presentation.begin_frame();
            auto* texture = game.game(context, frame, 640, 360, tick / 60.);
            presentation.end_frame(context);
            host->submit();
            game_pixels = test_sprite_draw::read_pixels(presentation.device(), context,
                                                        texture->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline, "Atlas Game preparation stalled");
            if (game.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (game.pending());
        check(game.diagnostics().empty() && game.game_scene() &&
                  game.game_scene()->sprites.size() == 6,
              "Atlas Game has incomplete or substituted Sprite resources");
        // Require visible image content in both views, not an identical blank
        // export that could hide a missing camera/material/Texture/frame table.
        for (const auto* pixels : {&scene_pixels, &game_pixels}) {
            const auto background = pixels->front();
            check(std::count_if(pixels->begin(), pixels->end(),
                                [&](auto p) { return p != background; }) > 1000,
                  "Atlas view contains no visible original images");
        }
        save_view_png(
            output / ("scene-" + std::to_string(tick) + ".png"), 640, 360,
            {reinterpret_cast<const std::uint8_t*>(scene_pixels.data()), scene_pixels.size() * 4});
        save_view_png(
            output / ("game-" + std::to_string(tick) + ".png"), 640, 360,
            {reinterpret_cast<const std::uint8_t*>(game_pixels.data()), game_pixels.size() * 4});
        scene_samples.push_back(std::move(scene_pixels));
        game_samples.push_back(std::move(game_pixels));
    }
    check(scene_samples.size() == 4 && scene_samples[0] == scene_samples[3] &&
              game_samples[0] == game_samples[3],
          "Gallery atlas loop did not restore exact initial pixels");
    check(scene_samples[0] != scene_samples[1] && scene_samples[1] != scene_samples[2] &&
              game_samples[0] != game_samples[1] && game_samples[1] != game_samples[2],
          "Gallery atlas animation did not show distinct sparse keys in both actual views");
    return std::pair{std::move(scene_samples), std::move(game_samples)};
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching worker");
        const auto root = test_sprite_atlas_gallery::prepare(argv[1], argv[2], argv[3]);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Atlas Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto images = root.parent_path() / "images";
        const auto authored = capture(presentation, context, root, images / "authored");
        const std::array roots{test_sprite_atlas_gallery::scene_id()};
        const auto target = desktop_texture_target();
        const auto content = root.parent_path() / "Content";
        package_runtime_content(root, content, roots, {target.platform, target.backend});
        const auto relocated = root.parent_path() / "Relocated Atlas Game";
        std::filesystem::rename(content, relocated);
        std::filesystem::rename(root, root.string() + "-source-offline");
        const auto exported = capture(presentation, context, relocated, images / "relocated");
        check(authored == exported,
              "Relocated source-free atlas Gallery changed actual Scene/Game pixels");
        std::ofstream receipt(images / "receipt.json");
        receipt << Json{{"scene", test_sprite_atlas_gallery::scene_id()},
                        {"exact_display_pairs", 8},
                        {"retained_scene_pick_framing", true},
                        {"mip_modes", {0, 1, 2}},
                        {"keys", {7, 41, 99}},
                        {"source_offline", true},
                        {"scope", "Actual software Vulkan Scene/Game; physical hardware and "
                                  "Windows remain unverified"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Atlas Gallery capture receipt failed");
        context->WaitForIdle();
        std::cout << "Gallery127 eight exact source-free Scene/Game pairs, retained redraw, "
                     "picking and framing passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
