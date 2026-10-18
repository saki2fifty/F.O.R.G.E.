#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "view_image_readback_tests.hpp"
#include <iostream>
int main(int argc, char** argv) {
    using namespace Diligent;
    try {
        forge::test_view_image::check(argc == 2, "Expected capture image directory");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        forge::test_view_image::check(device && context, "View capture Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        forge::test_view_image::exercise(presentation, context, argv[1]);
        std::cout << "Native view image capture passed: " << device->GetAdapterInfo().Description
                  << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
