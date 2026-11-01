#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_residency_gallery_fixture.hpp"
#include "view_image.hpp"
#include <forge/game_content.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
using test_tile_residency::check;
auto capture(DiligentPresentation& presentation, IDeviceContext* context,
             const std::filesystem::path& root, AssetId scene,
             const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    test_tile_residency::ready(world);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer renderer(presentation);
    renderer.resources(host);
    forge::Viewport preview(presentation);
    preview.resources(host);
    EditorCamera orbit;
    orbit.projection = CameraProjection::Orthographic;
    orbit.orthographic_height = 12;
    orbit.distance = 12;
    const auto document = world.scene.effective_document();
    std::vector<test_sprite_draw::Pixels> samples, scene_samples;
    for (unsigned tick = 0; tick <= 1200; ++tick) {
        if (tick)
            world.simulation.tick(1.f / 60);
        if (tick % 240)
            continue;
        const auto frame = test_tile_residency::sample(world, tick);
        orbit.target = {float(frame.cameras.front().world.m[3]), 0, 0};
        test_sprite_draw::Pixels scene_pixels;
        const auto scene_render = [&] {
            presentation.begin_frame();
            auto* result = preview.render(context, document, 640, 360, orbit, 1, false, {false});
            presentation.end_frame(context);
            host->submit();
            scene_pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, result->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
        };
        const auto scene_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do {
            scene_render();
            check(std::chrono::steady_clock::now() < scene_deadline,
                  "Scene Viewport chunk preparation stalled");
            if (preview.meshes()->pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (preview.meshes()->pending());
        const auto redraws = preview.redraws;
        scene_render();
        check(preview.meshes()->diagnostics().empty() && preview.redraws == redraws &&
                  preview.retained > 0,
              "Ready Scene Viewport did not retain its completed chunk frame");
        check(preview.pick(document, orbit, 640, 360, 110, 285) ==
                  frame.tile_maps.front().entity.str(),
              "Scene Viewport picking lost a ready sparse cell");
        save_view_png(
            output / ("scene-" + std::to_string(tick) + ".png"), 640, 360,
            {reinterpret_cast<const std::uint8_t*>(scene_pixels.data()), scene_pixels.size() * 4});
        scene_samples.push_back(std::move(scene_pixels));
        test_sprite_draw::Pixels pixels;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do {
            presentation.begin_frame();
            auto* target = renderer.game(context, frame, 640, 360, tick / 60.);
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, target->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < deadline,
                  "Residency Gallery page preparation stalled");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        // A coalesced ready request can finish in one update. Collect the former
        // Game view's now-expired bindings at the next ordinary owner pump.
        host->pump();
        const auto stats = host->inspect().tile_chunks;
        if (!(renderer.diagnostics().empty() && stats.selected == 2 && stats.pending == 0 &&
              stats.memory.total() == 2 * sizeof(TileMapChunkData))) {
            Json rows = Json::array();
            for (const auto& row : host->inspect().revisions)
                if (row.identity.variant.starts_with("tile-map:cells:"))
                    rows.push_back({{"variant", row.identity.variant},
                                    {"leases", row.strong_leases},
                                    {"state", resource_state_name(row.state)}});
            throw std::runtime_error("Actual Game chunk residency: " +
                                     Json{{"tick", tick},
                                          {"selected", stats.selected},
                                          {"pending", stats.pending},
                                          {"bytes", stats.memory.total()},
                                          {"pages", rows}}
                                         .dump());
        }
        check(std::count_if(pixels.begin(), pixels.end(),
                            [&](auto value) { return value != pixels.front(); }) > 100,
              "Residency Gallery camera displayed no painted region");
        save_view_png(output / (std::to_string(tick) + ".png"), 640, 360,
                      {reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size() * 4});
        samples.push_back(std::move(pixels));
    }
    check(samples.size() == 6 && samples.front() == samples.back(),
          "Looping camera did not reload exact original pixels");
    for (unsigned i = 1; i < 5; ++i)
        check(samples[i] != samples[i - 1],
              "Camera traversal did not reveal a distinct painted region");
    check(scene_samples.front() == scene_samples.back(),
          "Scene camera did not reload exact original pixels");
    auto framed = orbit;
    check(preview.frame_selection(document, {}, framed, 640.f / 360) && framed.target[0] > 100,
          "Scene framing lost complete offscreen Map bounds");
    return std::pair{std::move(samples), std::move(scene_samples)};
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_residency::prepare(argv[1], argv[2], argv[3]);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Residency Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto images = prepared.root.parent_path() / "images";
        const auto authored =
            capture(presentation, context, prepared.root, prepared.scene, images / "authored");
        const auto output = prepared.root.parent_path() / "Content";
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto relocated = prepared.root.parent_path() / "Relocated Game Content";
        std::filesystem::rename(output, relocated);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported =
            capture(presentation, context, relocated, prepared.scene, images / "relocated");
        check(authored == exported, "Source-free residency Gallery changed native pixels");
        std::ofstream receipt(images / "receipt.json");
        receipt << Json{{"scene", prepared.scene},
                        {"exact_display_pairs", 12},
                        {"scene_view_cache_pick_frame", true},
                        {"loaded_chunks_per_view", 2},
                        {"logical_chunks", 10},
                        {"scope", "Actual software Vulkan Game; no physical GPU/Windows/graphical "
                                  "EXE acceptance"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Residency Gallery native receipt failed");
        context->WaitForIdle();
        std::cout << "Residency Gallery: six exact source-free Game displays and two-page "
                     "residency PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
