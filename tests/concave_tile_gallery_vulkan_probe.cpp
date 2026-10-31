#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "concave_tile_scene_tests.hpp"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <set>
using namespace forge;
using namespace Diligent;
namespace {
using test_concave_tile_scene::check;
using test_gallery_capture::render;
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_concave_tile_scene::prepare(argv[1], argv[2], argv[3]);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Concave Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto captures = prepared.root.parent_path() / (prepared.scene.str() + "-images");
        const auto authored =
            render(presentation, context, prepared.root, prepared.scene, captures / "authored", 7);
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported =
            render(presentation, context, moved, prepared.scene, captures / "relocated", 7);
        check(authored == exported, "Relocated source-free concave tile Gallery displays changed");
        std::ofstream receipt(captures / "receipt.json");
        receipt << Json{{"scene", prepared.scene},
                        {"exact_display_pairs", 4},
                        {"source", test_concave_tile_scene::source},
                        {"scope", "Actual native Game gameplay/display on software Vulkan; no "
                                  "Windows, physical GPU or graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Concave Gallery display receipt write failed");
        context->WaitForIdle();
        std::cout << "Concave tile Gallery: actual animated Game and four exact source-free "
                     "relocated displays PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
