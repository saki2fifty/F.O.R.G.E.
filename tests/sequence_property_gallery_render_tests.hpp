#pragma once
#include "animation_playback_access.hpp"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include "view_image.hpp"
#include <chrono>
#include <cstdint>
#include <forge/animation.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace forge::test_sequence_properties {
using namespace Diligent;
using Pixels = test_sprite_draw::Pixels;
using test_sprite_draw::check;
inline constexpr auto source = "Scenes/183-sequence-property-crossfades.scene.json";
inline std::size_t differences(const Pixels& a, const Pixels& b, unsigned x0 = 0, unsigned y0 = 0,
                               unsigned x1 = 640, unsigned y1 = 360) {
    check(a.size() == 640 * 360 && b.size() == a.size(), "Property capture dimensions differ");
    std::size_t count = 0;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x)
            count += a[y * 640 + x] != b[y * 640 + x];
    return count;
}
inline std::vector<Pixels> capture(DiligentPresentation& presentation, IDeviceContext* context,
                                   const std::filesystem::path& root, AssetId scene,
                                   const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto ready = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!world.prepare_physics_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < ready, "Property world preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    world.simulation.reset_presentation();
    auto animation = animation_runtime(world.engine.world());
    EntityRef controller;
    const auto snapshot = world.scene.snapshot();
    for (const auto& row : snapshot.at("entities"))
        if (row.at("components").contains("forge.animation_sequence"))
            controller = {scene, row.at("id").get<EntityId>()};
    check(animation && controller.entity, "Property world has no animation controller");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    FrameRenderer game(presentation);
    game.resources(host);
    Viewport preview(presentation, true);
    preview.resources(host);
    EditorCamera orbit;
    orbit.target = {0, 1, 0};
    orbit.yaw = orbit.pitch = 0;
    orbit.distance = 12;
    orbit.projection = CameraProjection::Orthographic;
    orbit.orthographic_height = 8.5;
    std::vector<Pixels> results;
    unsigned generation = 0;
    for (const auto time : {0., 1.25, 1.5, 2.}) {
        AnimationPlaybackAccess::control(*animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", time}});
        const auto document = world.simulation.presentation(1);
        const auto values = extract_render_scene(document);
        check(values.meshes.size() == 2 && values.sprites.size() == 1 &&
                  values.cameras.size() == 1 && values.diagnostics.empty(),
              "Property capture composition differs");
        for (bool scene_view : {false, true}) {
            Pixels pixels;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            ++generation;
            do {
                presentation.begin_frame();
                auto* image = scene_view ? preview.render(context, document, 640, 360, orbit,
                                                          generation, false, {false})
                                         : game.game(context, values, 640, 360);
                check(image, "Property view has no image");
                presentation.end_frame(context);
                host->submit();
                pixels = test_sprite_draw::read_pixels(presentation.device(), context,
                                                       image->GetTexture());
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < deadline,
                      "Property drawing preparation stalled");
            } while (scene_view ? preview.meshes()->pending() : game.pending());
            check(scene_view ? preview.meshes()->diagnostics().empty()
                             : game.diagnostics().empty() && game.omitted_diagnostics() == 0,
                  "Property view reported a draw failure");
            save_view_png(
                output / ((scene_view ? "scene-" : "game-") + std::to_string(time) + ".png"), 640,
                360, {reinterpret_cast<const std::uint8_t*>(pixels.data()), pixels.size() * 4});
            results.push_back(std::move(pixels));
        }
    }
    check(differences(results[0], results[2]) > 100,
          "Continuous Light crossfade did not change Game pixels");
    // The unlit Sprite's opaque center stays fixed before the frame switch,
    // despite the Light change, then changes when the frame is selected.
    check(differences(results[1], results[3], 376, 172, 392, 188) == 0 &&
              differences(results[3], results[5], 376, 172, 392, 188) > 100,
          "Discrete Sprite frame did not reach the actual Scene pixels");
    return results;
}
inline void exercise(DiligentPresentation& presentation, IDeviceContext* context,
                     const std::filesystem::path& gallery, const std::filesystem::path& scratch) {
    const auto work = std::filesystem::absolute(scratch) / AssetId::generate().str();
    const auto root = work / "Project";
    std::filesystem::create_directories(work);
    std::filesystem::copy(gallery, root, std::filesystem::copy_options::recursive);
    std::ifstream stream(root / source);
    const auto document = Json::parse(stream);
    const auto scene = document.at("asset_id").get<AssetId>();
    const auto authored = capture(presentation, context, root, scene, work / "images/authored");
    const std::array roots{scene};
    {
        ProjectLease lease(root);
        prepare_runtime_content_catalog(lease, roots, {});
    }
    package_runtime_content(root, work / "Content", roots, {"windows", "d3d12"});
    std::filesystem::rename(work / "Content", work / "Relocated");
    std::filesystem::rename(root, work / "source-offline");
    const auto exported =
        capture(presentation, context, work / "Relocated", scene, work / "images/relocated");
    check(authored == exported, "Relocated property Scene/Game pixels differ");
    std::ofstream receipt(work / "receipt.json");
    receipt << Json{{"scene", scene},
                    {"exact_display_pairs", authored.size()},
                    {"source_offline", true},
                    {"continuous_game_pixels", true},
                    {"discrete_scene_pixels", true},
                    {"adapter", std::string(presentation.device()->GetAdapterInfo().Description)},
                    {"backend", int(presentation.device()->GetDeviceInfo().Type)},
                    {"scope",
                     "RuntimeWorld and actual Scene/Game renderers at explicit sequence seeks; "
                     "software graphics, no Windows/full-editor/physical acceptance"}}
                   .dump(2)
            << '\n';
    check(bool(receipt), "Property render receipt failed");
    context->WaitForIdle();
    std::cout << "Property crossfade authored/relocated Scene/Game pixels passed\n";
}
} // namespace forge::test_sequence_properties
