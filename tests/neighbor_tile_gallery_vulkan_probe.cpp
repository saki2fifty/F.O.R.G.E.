#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "neighbor_tile_scene_tests.hpp"
#include "runtime_package.hpp"
#include <iostream>

int main(int argc, char** argv) {
    using namespace forge;
    using namespace Diligent;
    using test_neighbor_tile_scene::check;
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_tile_gallery_scene::prepare(argv[1], argv[2], argv[3],
                                                               test_neighbor_tile_scene::source);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Neighbor Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto captures = prepared.root.parent_path() / "neighbor-displays";
        const auto authored = test_gallery_capture::render(
            presentation, context, prepared.root, prepared.scene, captures / "authored", 4);
        const std::array roots{prepared.scene};
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto relocated = test_gallery_capture::render(
            presentation, context, moved, prepared.scene, captures / "relocated", 4);
        check(authored == relocated, "Source-free neighbor Gallery display differs");
        std::ofstream out(captures / "receipt.json");
        out << Json{{"scene", prepared.scene},
                    {"exact_display_pairs", 4},
                    {"source", test_neighbor_tile_scene::source},
                    {"scope", "Native Game on software Vulkan; no Windows, physical GPU or "
                              "graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        check(bool(out), "Neighbor Gallery display receipt failed");
        context->WaitForIdle();
        std::cout << "Animated tile Game and four source-free relocated display pairs PASS\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
