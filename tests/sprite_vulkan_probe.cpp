#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "sprite_bundle_render_tests.hpp"
#include <iostream>

int main(int argc, char** argv) {
    using namespace Diligent;
    try {
        forge::test_sprite_draw::check(argc == 2, "Expected Sprite image output directory");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        forge::test_sprite_draw::check(device && context, "Sprite Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        const auto read = [&](ITexture* texture) {
            return forge::test_sprite_draw::read_pixels(device, context, texture);
        };
        forge::test_sprite_draw::exercise(presentation, context, read, argv[1]);
        forge::test_sprite_bundle::exercise(presentation, context, read);
        std::cout << "Sprite draw/bundle pixels and resource ownership passed: "
                  << device->GetAdapterInfo().Description << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
