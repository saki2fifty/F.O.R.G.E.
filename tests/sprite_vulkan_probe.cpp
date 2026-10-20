#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "sprite_atlas_precision_tests.hpp"
#include "sprite_bundle_render_tests.hpp"
#include "sprite_gallery_render_tests.hpp"
#include "sprite_image_render_tests.hpp"
#include "sprite_projection_render_tests.hpp"
#include "sprite_scene_render_tests.hpp"
#include "tile_map_render_tests.hpp"
#include <iostream>

int main(int argc, char** argv) {
    using namespace Diligent;
    try {
        forge::test_sprite_draw::check(
            argc == 2 || argc == 3, "Expected Sprite image output directory and optional Gallery");
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
        forge::test_sprite_image::exercise(presentation, context, read,
                                           std::filesystem::path(argv[1]) / "image-fill");
        forge::test_sprite_bundle::exercise(presentation, context, read);
        forge::test_sprite_atlas_precision::exercise(presentation, context, read);
        forge::test_sprite_scene_render::exercise(presentation, context, read,
                                                  std::filesystem::path(argv[1]) / "scene");
        forge::test_tile_map_render::exercise(presentation, context, read,
                                              std::filesystem::path(argv[1]) / "tile-map");
        forge::test_sprite_projection::exercise(presentation, context, read,
                                                std::filesystem::path(argv[1]) / "projection");
        if (argc == 3)
            forge::test_sprite_gallery_render::exercise(presentation, context, read, argv[2],
                                                        std::filesystem::path(argv[1]) / "gallery");
        std::cout << "Sprite draw/bundle pixels and resource ownership passed: "
                  << device->GetAdapterInfo().Description << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
