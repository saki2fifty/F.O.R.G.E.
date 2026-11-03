#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include "sequence_frame_render_tests.hpp"
#include <iostream>

using namespace forge;
using namespace Diligent;
int main(int argc, char** argv) {
    try {
        const bool frames = argc == 4 && std::string_view(argv[3]) == "--sequence-frames";
        test_gallery_capture::check(argc == 3 || frames,
                                    "Need Gallery and transition capture scratch root");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto root = scratch / "Gallery";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        Json source;
        std::ifstream(root / "Scenes/153-camera-transitions.scene.json") >> source;
        const auto id = source.at("asset_id").get<AssetId>();
        EngineVkCreateInfo create;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        test_gallery_capture::check(device && context, "Transition Vulkan device unavailable");
        DiligentPresentation presentation(device);
        if (frames) {
            test_sequence_frame_render::exercise(presentation, context, root, scratch / "frames",
                                                 true);
            std::cout
                << "Stopped Timeline transition display and exported PNG pixels match exactly\n";
            return 0;
        }
        const auto render = [&](const auto& project, const auto& output) {
            return test_gallery_capture::render(presentation, context, project, id, output, 8, true,
                                                {120, 150, 180});
        };
        const auto authored = render(root, scratch / "authored");
        const std::array roots{id};
        const auto package = scratch / "Content";
        package_runtime_content(root, package, roots, {"windows", "d3d12"});
        const auto relocated = scratch / "Relocated game";
        std::filesystem::rename(package, relocated);
        std::filesystem::rename(root, scratch / "SourcesOffline");
        const auto exported = render(relocated, scratch / "relocated");
        test_gallery_capture::check(authored == exported,
                                    "Relocated transition pixels differ from the source display");
        std::cout
            << "Actual animated Game transition frames match source-hidden relocated export\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
