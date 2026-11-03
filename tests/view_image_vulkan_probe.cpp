#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "view_image_editor_tests.hpp"
#include "view_image_readback_tests.hpp"
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    using namespace Diligent;
    try {
        const bool editor = argc == 3 && std::string(argv[2]) == "--ui";
        forge::test_view_image::check(argc == 2 || editor,
                                      "Expected capture image directory and optional --ui");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        forge::test_view_image::check(device && context, "View capture Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        if (editor)
            for (float scale : {1.f, 1.5f, 2.f})
                for (const auto size : {ImVec2{1400, 900}, ImVec2{960, 640}})
                    forge::test_view_image_ui::exercise(presentation, context, argv[1], scale,
                                                        size);
        else
            forge::test_view_image::exercise(presentation, context, argv[1]);
        std::cout << "Native view image capture passed: " << device->GetAdapterInfo().Description
                  << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
