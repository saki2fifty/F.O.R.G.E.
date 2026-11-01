#pragma once
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include "runtime_tile_map.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include "view_image.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
namespace forge::test_sprite_bloom_gallery {
using namespace Diligent;
using Pixels = test_sprite_draw::Pixels;
using test_sprite_draw::check;
inline constexpr auto source = "Scenes/128-sprite-tile-bloom.scene.json";
inline std::vector<Pixels> capture(DiligentPresentation& presentation, IDeviceContext* context,
                                   const std::filesystem::path& root, AssetId id,
                                   const std::filesystem::path& out) {
    std::filesystem::create_directories(out);
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {id}));
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.prepare_physics_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline, "Glow Scene resources stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer game(presentation);
    game.resources(host);
    forge::Viewport preview(presentation, true);
    preview.resources(host);
    EditorCamera orbit;
    orbit.target = {0, 0, 0};
    orbit.yaw = orbit.pitch = 0;
    orbit.distance = 12;
    orbit.projection = CameraProjection::Orthographic;
    orbit.orthographic_height = 8.5;
    auto frame = extract_render_scene(world.simulation.presentation(1));
    detail::bind_runtime_tile_maps(world.engine.world(), frame);
    check(frame.sprites.size() == 3 && frame.tile_maps.size() == 1 && frame.cameras.size() == 1 &&
              frame.meshes.empty() && frame.diagnostics.empty(),
          "Glow Scene lost Sprite/Tile/camera content");
    check(frame.settings.bloom == SceneBloom{true, .25f, 1.25f, 3},
          "Glow Scene lost authored bloom settings");
    std::vector<Pixels> result;
    unsigned generation = 0;
    auto draw = [&](bool scene_view, RenderScene values, Json document, const std::string& name) {
        Pixels pixels;
        const auto ready = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        ++generation;
        do {
            presentation.begin_frame();
            auto* image = scene_view ? preview.render(context, document, 640, 360, orbit,
                                                      generation, false, {false})
                                     : game.game(context, values, 640, 360);
            check(image, "Glow view has no image");
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < ready, "Glow drawing preparation stalled");
        } while (scene_view ? preview.meshes()->pending() : game.pending());
        if (scene_view)
            check(preview.meshes()->diagnostics().empty(), "Glow Scene draw failed");
        else
            check(game.diagnostics().empty() && game.omitted_diagnostics() == 0,
                  "Glow Game draw failed");
        save_view_png(out / (name + ".png"), 640, 360,
                      {reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size() * 4});
        result.push_back(pixels);
        return pixels;
    };
    const auto outer_glow = [&](const Pixels& off, const Pixels& on, const char* why) {
        const auto background = off.front();
        unsigned changed = 0, outer = 0;
        for (std::size_t i = 0; i < off.size(); ++i) {
            bool different = false;
            for (unsigned c = 0; c < 3; ++c)
                different |= std::abs(int(off[i][c]) - int(on[i][c])) > 2;
            changed += different;
            outer += different && off[i] == background;
            check(off[i][3] == on[i][3], "Bloom changed composed view alpha");
        }
        std::cout << why << ": changed " << changed << ", glow outside footprint " << outer << '\n';
        check(changed > 100 && outer > 100, why);
    };
    const auto effective = world.scene.effective_document();
    for (bool scene_view : {false, true}) {
        auto disabled = frame;
        disabled.settings.bloom.enabled = false;
        auto off_doc = effective;
        off_doc["rendering"]["bloom"]["enabled"] = false;
        const auto prefix = scene_view ? "scene" : "game";
        const auto off = draw(scene_view, disabled, off_doc, std::string(prefix) + "-off");
        const auto on = draw(scene_view, frame, effective, std::string(prefix) + "-glow");
        outer_glow(off, on,
                   scene_view ? "Actual HDR Scene Sprite/Tile glow missing"
                              : "Actual Game Sprite/Tile glow missing");
        if (!scene_view) {
            auto zero = frame;
            zero.settings.bloom.intensity = 0;
            check(draw(false, zero, effective, "game-zero-intensity") == off,
                  "Zero intensity did not bypass bloom exactly");
            auto high = frame;
            high.settings.bloom.threshold = 2;
            check(draw(false, high, effective, "game-high-threshold") == off,
                  "Subthreshold Sprite/Tile colors incorrectly glow");
            for (unsigned layer : {1u, 2u}) {
                auto isolated = frame;
                isolated.cameras[0].camera.layers = layer;
                auto isolated_off = isolated;
                isolated_off.settings.bloom.enabled = false;
                const auto a = draw(false, isolated_off, effective,
                                    "game-layer" + std::to_string(layer) + "-off");
                const auto b = draw(false, isolated, effective,
                                    "game-layer" + std::to_string(layer) + "-glow");
                outer_glow(a, b,
                           layer == 1 ? "Isolated Sprite layer has no glow"
                                      : "Isolated Tile layer has no glow");
            }
            auto bounded = frame;
            bounded.cameras[0].camera.viewport_width = .5;
            auto bounded_off = bounded;
            bounded_off.settings.bloom.enabled = false;
            const auto before = draw(false, bounded_off, effective, "game-half-view-off");
            const auto after = draw(false, bounded, effective, "game-half-view-glow");
            unsigned inside = 0;
            for (unsigned y = 0; y < 360; ++y)
                for (unsigned x = 0; x < 640; ++x) {
                    const auto at = std::size_t(y) * 640 + x;
                    if (x >= 320)
                        check(before[at] == after[at],
                              "Sprite/Tile bloom escaped its camera rectangle");
                    else
                        inside += before[at] != after[at];
                }
            check(inside > 100, "Bounded Sprite/Tile view did not apply glow");
        }
    }
    return result;
}
inline void exercise(DiligentPresentation& presentation, IDeviceContext* context,
                     const std::filesystem::path& gallery, const std::filesystem::path& scratch,
                     const std::filesystem::path& worker) {
    const auto prepared = test_tile_gallery_scene::prepare(
        gallery, std::filesystem::absolute(scratch), worker, source);
    const auto images = prepared.root.parent_path() / "images";
    const auto authored =
        capture(presentation, context, prepared.root, prepared.scene, images / "authored");
    const std::array roots{prepared.scene};
    const auto target = desktop_texture_target();
    const auto content = prepared.root.parent_path() / "Content";
    package_runtime_content(prepared.root, content, roots, {target.platform, target.backend});
    const auto relocated = prepared.root.parent_path() / "Relocated Glow Game";
    std::filesystem::rename(content, relocated);
    std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
    const auto exported =
        capture(presentation, context, relocated, prepared.scene, images / "relocated");
    check(authored == exported, "Source-free relocated Sprite/Tile glow changed Scene/Game pixels");
    std::ofstream receipt(images / "receipt.json");
    receipt
        << Json{{"scene", prepared.scene},
                {"exact_display_pairs", authored.size()},
                {"sprites", 3},
                {"tile_maps", 1},
                {"source_offline", true},
                {"backend", int(presentation.device()->GetDeviceInfo().Type)},
                {"adapter", std::string(presentation.device()->GetAdapterInfo().Description)},
                {"scope",
                 "Actual FORGE Scene/Game renderer; physical hardware acceptance remains separate"}}
               .dump(2)
        << '\n';
    check(bool(receipt), "Glow receipt write failed");
    context->WaitForIdle();
    std::cout << "Sprite/Tile bloom authored and relocated Scene/Game output PASS\n";
}
} // namespace forge::test_sprite_bloom_gallery
