#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "gallery_game_capture.hpp"
#include "runtime_package.hpp"
#include <iostream>
using namespace forge;
using namespace Diligent;
using test_gallery_capture::check;
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Need Gallery and scratch directory");
        const auto scratch = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        const auto source = scratch / "Source";
        std::filesystem::create_directories(scratch);
        std::filesystem::copy(argv[1], source, std::filesystem::copy_options::recursive);
        const auto catalog = AssetCatalog::open_project(source);
        AssetId scene;
        for (const auto& [id, record] : catalog.records())
            if (record.source == "Scenes/175-fbx-model-import.scene.json")
                scene = id;
        check(bool(scene), "FBX Gallery scene is not registered");
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "FBX Gallery Vulkan device unavailable");
        DiligentPresentation presentation(device);
        constexpr std::array<unsigned, 3> ticks{6, 18, 42};
        const auto original = test_gallery_capture::render(presentation, context, source, scene,
                                                           scratch / "authored", 21, true, ticks);
        const std::array roots{scene};
        package_runtime_content(source, scratch / "Content", roots, {"windows", "d3d12"});
        const auto moved = scratch / "Relocated";
        std::filesystem::rename(scratch / "Content", moved);
        std::filesystem::rename(source, scratch / "SourcesOffline");
        const auto exported = test_gallery_capture::render(presentation, context, moved, scene,
                                                           scratch / "relocated", 21, true, ticks);
        check(original == exported, "FBX source-hidden relocated pixels differ");
        check(!std::filesystem::exists(moved / "Assets"),
              "FBX runtime package contains raw sources");
        context->WaitForIdle();
        std::ofstream(scratch / "receipt.json")
            << Json{{"scene", scene},
                    {"exact_image_pairs", original.size()},
                    {"ticks", ticks},
                    {"scope", "Shared native runtime/FrameRenderer on software Vulkan, with "
                              "actual imported transforms/linear skins and source-hidden content; "
                              "no Windows, physical-device or graphical-EXE acceptance"}}
                   .dump(2)
            << '\n';
        std::cout << "FBX Gallery actual native pixels and four relocated source-hidden image "
                     "pairs passed: "
                  << scratch << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
