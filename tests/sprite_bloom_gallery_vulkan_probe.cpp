#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "sprite_bloom_gallery_render_tests.hpp"
#include <iostream>
int main(int argc, char** argv) {
    try {
        forge::test_sprite_draw::check(argc == 4, "Need Gallery, scratch and matching worker");
        Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
        Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
        Diligent::EngineVkCreateInfo info;
        Diligent::GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        forge::test_sprite_draw::check(device && context, "Sprite bloom Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        forge::test_sprite_bloom_gallery::exercise(presentation, context, argv[1], argv[2],
                                                   argv[3]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
