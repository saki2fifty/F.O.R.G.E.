#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>
using namespace forge;
using namespace Diligent;
int main(int argc, char** argv) {
    try {
        test_gallery_capture::check(argc == 3 || argc == 4,
                                    "Need prepared Gallery, image scratch root and optional Scene");
        const std::string source_path =
            argc == 4 ? argv[3] : "Scenes/136-navigation-polygons.scene.json";
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_gallery_capture::check(device && context, "Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        Json source;
        std::ifstream(root / source_path) >> source;
        const auto id = source.at("asset_id").get<AssetId>();
        const auto authored =
            test_gallery_capture::render(presentation, context, root, id, scratch / "authored", 6);
        const auto package = scratch / "Content";
        const std::array roots{id};
        (void)package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated game", hidden = scratch / "SourcesOffline";
        std::filesystem::rename(package, moved);
        std::filesystem::rename(root, hidden);
        const auto exported = test_gallery_capture::render(presentation, context, moved, id,
                                                           scratch / "relocated", 6);
        test_gallery_capture::check(authored == exported, "Source-free Game displays changed");
        std::ofstream receipt(scratch / "receipt.json");
        receipt << Json{{"scene", id},
                        {"exact_display_pairs", 4},
                        {"source", source_path},
                        {"scope", "Actual moving Game frames on software Vulkan; no "
                                  "Windows, physical GPU or graphical EXE acceptance"}}
                       .dump(2)
                << '\n';
        test_gallery_capture::check(bool(receipt), "Gallery Game receipt write failed");
        context->WaitForIdle();
        std::cout << "Gallery: four exact animated source-free relocated Game pairs passed: "
                  << scratch << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
