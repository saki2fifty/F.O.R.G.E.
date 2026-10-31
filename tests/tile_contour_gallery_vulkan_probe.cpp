#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <set>
using namespace forge;
using namespace Diligent;
namespace {
using test_tile_asset_io::check;
constexpr auto source = "Scenes/123-tile-collision-contours.scene.json";
using test_gallery_capture::render;
void verify_course(const std::vector<test_gallery_capture::Pixels>& captures) {
    check(captures.size() == 4, "Contour Gallery lost a requested display");
    for (unsigned i = 0; i < 3; ++i) {
        const auto& pixels = captures[i];
        const auto at = [&](double x, double y) {
            // Authored camera: center(2,-1.8), height10; 640x360 auto aspect.
            const auto px = unsigned(std::floor(320 + (x - 2) * 36));
            const auto py = unsigned(std::floor(180 - (y + 1.8) * 36));
            return pixels.at(py * 640 + px);
        };
        const auto background = at(2, 2), ring = at(-3.5, -2), bar = at(4.5, -2);
        check(ring != background && bar != background && ring != bar && at(-2, -2) == ring &&
                  at(-2.7, -2) == background && at(7.5, -2) == bar && at(6, -2) == background,
              "Displayed ring/void/island/separate bars disagree with authored contours");
        const auto floor = at(2, -6);
        check(floor != background && floor != ring && floor != bar,
              "Contour Gallery lost its catch floor");
        const std::set<std::array<std::uint8_t, 4>> colors(pixels.begin(), pixels.end());
        check(colors.size() >= 8, "Landscape contour display lost its actor/course colors");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_gallery_scene::prepare(argv[1], argv[2], argv[3], source);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Contour Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto captures = prepared.root.parent_path() / (prepared.scene.str() + "-images");
        // Auto-aspect portrait crops the actors; it retains four course colors.
        const auto authored =
            render(presentation, context, prepared.root, prepared.scene, captures / "authored", 4);
        verify_course(authored);
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported =
            render(presentation, context, moved, prepared.scene, captures / "relocated", 4);
        verify_course(exported);
        check(authored == exported, "Relocated source-free contour tile Gallery displays changed");
        std::ofstream receipt(captures / "receipt.json");
        receipt << Json{{"scene", prepared.scene},
                        {"exact_display_pairs", 4},
                        {"source", source},
                        {"scope", "Actual native Game gameplay/display on software Vulkan; no "
                                  "Windows, physical GPU or graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Contour Gallery display receipt write failed");
        context->WaitForIdle();
        std::cout << "Contour tile Gallery: actual Game and four exact source-free "
                     "relocated displays PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
