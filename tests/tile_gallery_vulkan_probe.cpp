#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "tile_gallery_render_tests.hpp"
#include <iostream>
int main(int argc, char** argv) {
    using namespace Diligent;
    try {
        forge::test_sprite_draw::check(argc == 3, "Need Gallery and image output root");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        forge::test_sprite_draw::check(device && context, "Tile Gallery Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        const auto read = [&](ITexture* image) {
            return forge::test_sprite_draw::read_pixels(device, context, image);
        };
        forge::test_tile_gallery_render::exercise(presentation, context, read, argv[1], argv[2]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
