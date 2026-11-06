#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>
using namespace forge;
int main(int argc, char** argv) {
    using namespace Diligent;
    using test_gallery_capture::check;
    try {
        check(argc == 3 || argc == 5,
              "Need animated Gallery and image scratch, optionally scene and source paths");
        const auto scene_path = argc == 5 ? argv[3] : "Scenes/179-animation-retargeting.scene.json";
        const auto source_path =
            argc == 5 ? argv[4] : "Assets/AnimationRetarget/Swing.retarget.json";
        // The graph comparison reuses three gold materials; the original
        // retarget scene also includes the blue rig's separate palette.
        const auto minimum_colors = argc == 5 ? 4u : 5u;
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "SourceProject";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        Json scene;
        std::ifstream(source / scene_path) >> scene;
        const auto id = scene.at("asset_id").get<AssetId>();
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        info.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Animated Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto authored = test_gallery_capture::render(presentation, context, source, id,
                                                           scratch / "authored", minimum_colors);
        for (std::size_t sample = 0; sample < authored.size(); ++sample) {
            const auto width = sample == 3 ? 360u : 640u;
            const auto height = sample == 3 ? 640u : 360u;
            const auto background = authored[sample].front();
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                    if (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height)
                        check(authored[sample][std::size_t(y) * width + x] == background,
                              "Gallery animation touches the display border");
        }
        const std::array roots{id};
        package_runtime_content(source, scratch / "Export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(source, scratch / "SourcesOffline");
        check(!std::filesystem::exists(scratch / "Relocated" / source_path),
              "Animated runtime export retained editable source");
        const auto exported =
            test_gallery_capture::render(presentation, context, scratch / "Relocated", id,
                                         scratch / "relocated-images", minimum_colors);
        check(authored == exported && authored.size() == 4,
              "Animation changed after source-hidden relocation");
        const Json receipt{{"scene", id},
                           {"exact_display_pairs", authored.size()},
                           {"scope",
                            "Actual animated RuntimeWorld/FrameRenderer on software Vulkan, "
                            "Windows-target content relocated with sources offline; no Windows, "
                            "physical GPU or graphical EXE acceptance"}};
        std::ofstream(scratch / "receipt.json") << receipt.dump(2) << '\n';
        context->WaitForIdle();
        std::cout << receipt.dump() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
