#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>
using namespace forge;
int main(int argc, char** argv) {
    using namespace Diligent;
    using test_gallery_capture::check;
    try {
        check(argc == 3, "Need retarget Gallery and image scratch");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "SourceProject";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        Json scene;
        std::ifstream(source / "Scenes/179-animation-retargeting.scene.json") >> scene;
        const auto id = scene.at("asset_id").get<AssetId>();
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        info.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Retarget Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const auto authored = test_gallery_capture::render(presentation, context, source, id,
                                                           scratch / "authored", 5);
        for (std::size_t sample = 0; sample < authored.size(); ++sample) {
            const auto width = sample == 3 ? 360u : 640u;
            const auto height = sample == 3 ? 640u : 360u;
            const auto background = authored[sample].front();
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                    if (x < 2 || y < 2 || x + 2 >= width || y + 2 >= height)
                        check(authored[sample][std::size_t(y) * width + x] == background,
                              "Retarget Gallery animation touches the display border");
        }
        const std::array roots{id};
        package_runtime_content(source, scratch / "Export", roots, {"windows", "d3d12"});
        std::filesystem::rename(scratch / "Export", scratch / "Relocated");
        std::filesystem::rename(source, scratch / "SourcesOffline");
        check(!std::filesystem::exists(scratch /
                                       "Relocated/Assets/AnimationRetarget/Swing.retarget.json"),
              "Retarget runtime export retained editable recipe");
        const auto exported = test_gallery_capture::render(
            presentation, context, scratch / "Relocated", id, scratch / "relocated-images", 5);
        check(authored == exported && authored.size() == 4,
              "Retarget animation changed after source-hidden relocation");
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
