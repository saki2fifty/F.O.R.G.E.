#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "moving_one_way_scene_tests.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <set>
using namespace forge;
using namespace Diligent;
namespace {
using test_gallery_capture::render;
using test_moving_one_way_scene::check;
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Gallery, scratch and matching asset worker");
        const auto prepared = test_moving_one_way_scene::prepare(argv[1], argv[2]);
        test_moving_one_way_scene::settle(prepared, argv[3]);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Moving Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto captures = prepared.root.parent_path() / (prepared.scene.str() + "-images");
        const auto authored =
            render(presentation, context, prepared.root, prepared.scene, captures / "authored");
        const auto output = prepared.root.parent_path() / (prepared.scene.str() + "-content");
        const std::array roots{prepared.scene};
        const auto target = desktop_texture_target();
        package_runtime_content(prepared.root, output, roots, {target.platform, target.backend});
        const auto moved = prepared.root.parent_path() / (prepared.scene.str() + " Relocated");
        std::filesystem::rename(output, moved);
        std::filesystem::rename(prepared.root, prepared.root.string() + "-source-offline");
        const auto exported =
            render(presentation, context, moved, prepared.scene, captures / "relocated");
        check(authored == exported, "Relocated source-free moving Gallery displays changed");
        std::ofstream receipt(captures / "receipt.json");
        receipt << Json{{"scene", prepared.scene},
                        {"exact_display_pairs", 4},
                        {"source", test_moving_one_way_scene::source},
                        {"scope", "Actual native Game gameplay/display on software Vulkan; no "
                                  "Windows, physical GPU or graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        check(bool(receipt), "Moving Gallery display receipt write failed");
        context->WaitForIdle();
        std::cout << "Moving one-way Gallery: actual animated Game and four exact source-free "
                     "relocated displays PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
