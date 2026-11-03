#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>

using namespace forge;
using namespace Diligent;
int main(int argc, char** argv) {
    try {
        test_gallery_capture::check(argc == 3, "Need prepared Gallery and capture scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(root / "Scenes/152-camera-render-textures.scene.json") >> source;
        const auto id = source.at("asset_id").get<AssetId>();
        EngineVkCreateInfo create;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_gallery_capture::check(device && context, "Camera Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        // Nonperiodic samples avoid aliasing a symmetric rotating cube's silhouette.
        const auto render = [&](const auto& project, const auto& output) {
            return test_gallery_capture::render(presentation, context, project, id, output, 3, true,
                                                {1, 30, 75});
        };
        const auto authored = render(root, scratch / "authored");
        const auto& pixels = authored.front();
        for (const auto x : {84u, 436u}) {
            const auto& red = pixels.at(180 * 640 + x);
            const auto& cyan = pixels.at(180 * 640 + x + 120);
            test_gallery_capture::check(red[0] > red[1] + 50 && red[0] > red[2] + 50 &&
                                            cyan[2] > cyan[0] + 50 && cyan[1] > cyan[0] + 50,
                                        "Gallery split/relay monitor lost its red and cyan images");
        }
        const std::array roots{id};
        const auto package = scratch / "Content";
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated game";
        std::filesystem::rename(package, moved);
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto exported = render(moved, scratch / "relocated");
        test_gallery_capture::check(authored == exported,
                                    "Relocated source-hidden camera Gallery displays changed");
        std::ofstream(scratch / "receipt.json")
            << Json{{"scene", id},
                    {"sample_ticks", {1, 30, 75}},
                    {"exact_display_pairs", 4},
                    {"scope", "Production animated split/relay Game images on software Vulkan; "
                              "no Windows, physical GPU or graphical EXE acceptance"}}
                   .dump(2)
            << '\n';
        context->WaitForIdle();
        std::cout << "Camera Gallery: four animated split/relay source-hidden relocated display "
                     "pairs passed: "
                  << scratch << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
