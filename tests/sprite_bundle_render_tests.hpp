#pragma once
#include "mesh_draw_bundle.hpp"
#include "sprite_draw_render_tests.hpp"
#include "sprite_resource_fixture.hpp"

namespace forge::test_sprite_bundle {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto source = test_sprite_resources::image();
    source.subresources[0].clear();
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 8; ++x)
            for (auto lane : test_sprite_draw::texel(x / 4, x % 4, y))
                source.subresources[0].push_back(std::byte(lane));
    auto* device = presentation.device();
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE Sprite bundle ownership proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Sprite bundle proof target unavailable");
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 1;
    AffineTransform camera_world;
    camera_world.m[11] = -2;
    const auto view = camera_view(camera, camera_world, 64, 64);
    for (auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Mask, MaterialAlpha::Blend}) {
        test_sprite_resources::Fixture fixture(source, alpha);
        GpuResidency<MeshAsset> meshes(device, context, 1024 * 1024);
        GpuResidency<TextureAsset> textures(device, context, 1024 * 1024);
        MeshDrawBundle sprite(presentation, context, fixture.draw, meshes, textures,
                              TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, false, {},
                              MeshDrawProfile::Sprite);
        MeshDrawBundle ordinary(presentation, context, fixture.draw, meshes, textures,
                                TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
        check(sprite.profile() == MeshDrawProfile::Sprite &&
                  ordinary.profile() == MeshDrawProfile::Mesh &&
                  sprite.mesh_identity() == ordinary.mesh_identity() &&
                  meshes.statistics().resident == 1 && textures.statistics().resident == 1,
              "Sprite specialization duplicated or aliased resource ownership");
        const auto render = [&](const SpriteDrawValues& values) {
            presentation.begin_frame();
            auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            context->SetRenderTargets(1, &rtv, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                      RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            constexpr float clear[]{8.f / 255, 16.f / 255, 32.f / 255, 1};
            context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const Diligent::Viewport viewport{0, 0, 64, 64, 0, 1};
            context->SetViewports(1, &viewport, 64, 64);
            sprite.draw_part(context, {}, view, {}, 0, 0, nullptr, nullptr, nullptr, {}, nullptr,
                             nullptr, {}, {}, &values);
            presentation.end_frame(context);
            meshes.submit();
            textures.submit();
            auto pixels = readback(color);
            context->FinishFrame();
            device->ReleaseStaleResources();
            return pixels;
        };
        for (unsigned frame = 0; frame < 2; ++frame) {
            SpriteDrawValues values{
                sprite_region_mapping(sprite_grid_region(8, 4, {2, 1}, frame), 8, 4, true),
                {.5f, .75f, .25f, .5f}};
            const auto before = render(values);
            for (unsigned y = 0; y < 64; ++y)
                for (unsigned x = 0; x < 64; ++x) {
                    auto expected =
                        test_sprite_draw::filtered(frame, 1 - (x + .5) / 64, (y + .5) / 64, false);
                    for (unsigned lane = 0; lane < 4; ++lane)
                        expected[lane] *= values.color[lane];
                    constexpr double clear[]{8. / 255, 16. / 255, 32. / 255, 1};
                    for (unsigned lane = 0; lane < 4; ++lane) {
                        auto output = expected[lane];
                        if (alpha == MaterialAlpha::Mask && expected[3] < .5)
                            output = clear[lane];
                        else if (alpha == MaterialAlpha::Blend)
                            output = (lane == 3 ? expected[3] : expected[lane] * expected[3]) +
                                     clear[lane] * (1 - expected[3]);
                        else if (lane == 3)
                            output = 1;
                        check(std::abs(int(before[y * 64 + x][lane]) -
                                       int(std::lround(output * 255))) <= 1,
                              "Sprite bundle pixels differ from independent reference");
                    }
                }
            auto bad_source = source;
            bad_source.alpha = TextureAlpha::Premultiplied;
            test_sprite_resources::Fixture bad(bad_source, alpha);
            bool rejected = false;
            try {
                MeshDrawBundle incompatible(presentation, context, bad.draw, meshes, textures,
                                            TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, false, {},
                                            MeshDrawProfile::Sprite);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && meshes.statistics().resident == 1 &&
                      textures.statistics().resident == 1 && render(values) == before,
                  "Failed Sprite candidate allocated resources or damaged retained bundle");
        }
    }
}
} // namespace forge::test_sprite_bundle
