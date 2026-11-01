#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>
using namespace forge;
using namespace Diligent;
int main(int argc, char** argv) {
    try {
        test_gallery_capture::check(argc == 3, "Need prepared Gallery and image scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_gallery_capture::check(device && context,
                                    "Behavior Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        const char* paths[]{"Scenes/131-patrol-behavior.scene.json",
                            "Scenes/132-chase-behavior-xy.scene.json"};
        for (unsigned i = 0; i < 2; ++i) {
            Json source;
            std::ifstream(root / paths[i]) >> source;
            const auto id = source.at("asset_id").get<AssetId>();
            const auto output = scratch / (i ? "chase" : "patrol");
            const auto authored = test_gallery_capture::render(presentation, context, root, id,
                                                               output / "authored", 6);
            const auto package = output / "Content";
            const std::array roots{id};
            (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
            const auto moved = output / "Relocated game", hidden = scratch / "SourcesOffline";
            std::filesystem::rename(package, moved);
            std::filesystem::rename(root, hidden);
            const auto exported = test_gallery_capture::render(presentation, context, moved, id,
                                                               output / "relocated", 6);
            test_gallery_capture::check(authored == exported,
                                        "Source-free behavior Game displays changed");
            std::filesystem::rename(hidden, root);
            std::ofstream receipt(output / "receipt.json");
            receipt << Json{{"scene", id},
                            {"exact_display_pairs", 4},
                            {"source", paths[i]},
                            {"scope", "Actual moving behavior Game frames on software Vulkan; no "
                                      "Windows, physical GPU or graphical EXE acceptance"}}
                           .dump(2)
                    << '\n';
            test_gallery_capture::check(bool(receipt), "Behavior Game receipt write failed");
        }
        context->WaitForIdle();
        std::cout
            << "Behavior Gallery: eight exact animated source-free relocated Game pairs passed: "
            << scratch << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
