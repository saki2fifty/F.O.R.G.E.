#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "animated_tile_scene_tests.hpp"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>

int main(int argc, char** argv) {
    using namespace forge;
    using namespace Diligent;
    using test_animated_tile_scene::check;
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_gallery_scene::prepare(argv[1], argv[2], argv[3],
                                                               test_animated_tile_scene::source);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Animated Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto captures = prepared.root.parent_path() / "animated-displays";
        const auto authored = test_gallery_capture::render(
            presentation, context, prepared.root, prepared.scene, captures / "authored", 4);
        const auto center = [&](unsigned sample, unsigned row) {
            return authored.at(sample).at((90 + 60 * row) * 640 + 245);
        };
        // Independent lane ordering at 30/60/90 ticks. Compare display colors
        // after the real output transform without assuming a physical GPU transfer curve.
        const auto blue = center(0, 3), orange = center(0, 0), pink = center(0, 2),
                   green = center(1, 2);
        check((std::set{blue, orange, pink, green}.size() == 4) && center(0, 1) == blue &&
                  center(1, 0) == blue && center(1, 1) == orange && center(1, 3) == blue &&
                  center(2, 0) == pink && center(2, 1) == green && center(2, 2) == pink &&
                  center(2, 3) == blue,
              "Rendered forward/reverse/held/static lane frame order differs");
        const std::array roots{prepared.scene};
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto relocated = test_gallery_capture::render(
            presentation, context, moved, prepared.scene, captures / "relocated", 4);
        check(authored == relocated, "Source-free animated Gallery display differs");
        std::ofstream out(captures / "receipt.json");
        out << Json{{"scene", prepared.scene},
                    {"exact_display_pairs", 4},
                    {"source", test_animated_tile_scene::source},
                    {"scope", "Native Game on software Vulkan; no Windows, physical GPU or "
                              "graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        check(bool(out), "Animated Gallery display receipt failed");
        context->WaitForIdle();
        std::cout << "Animated tile Game and four source-free relocated display pairs PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
