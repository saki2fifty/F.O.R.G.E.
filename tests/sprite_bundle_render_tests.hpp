#pragma once
#include "mesh_draw_bundle.hpp"
#include "sprite_draw_render_tests.hpp"
#include "sprite_render_values.hpp"
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
        for (unsigned mode = 0; mode <= 2; ++mode)
            for (bool flip_x : {false, true})
                for (bool flip_y : {false, true}) {
                    SpriteRenderer cropped;
                    cropped.region_enabled = true;
                    cropped.region_x = cropped.region_y = 1;
                    cropped.region_width = 5;
                    cropped.region_height = 2;
                    cropped.flip_x = flip_x;
                    cropped.flip_y = flip_y;
                    cropped.image_mode = mode;
                    // Keep every sample away from point-addressing quantization
                    // ties (Direct3D permits8fractional subtexel bits). Density10
                    // placed Sliced x40 only1/512texel below a boundary; WARP
                    // legitimately selected its neighbor. Retain every assertion.
                    cropped.pixels_per_meter = 8;
                    cropped.border_left = cropped.border_right = 1;
                    const auto pixels = render(sprite_draw_values(cropped, 8, 4));
                    for (unsigned y = 0; y < 64; ++y)
                        for (unsigned x = 0; x < 64; ++x) {
                            // Independent pixel-space sampling of the asymmetric5x2
                            // rectangle, crossing the original four-pixel cells.
                            const auto u = (flip_x ? 63.5 - x : x + .5) / 64;
                            const auto v = (flip_y ? 63.5 - y : y + .5) / 64;
                            double rx = u * 5, ry = v * 2;
                            if (mode == 1) {
                                const auto p = u * 8;
                                rx = p < 1 ? p : p >= 7 ? p - 3 : 1 + (p - 1) * 3 / 6;
                            } else if (mode == 2) {
                                rx = std::fmod(u * 8, 5.);
                                ry = std::fmod(v * 8, 2.);
                            }
                            const auto sx = 1 + unsigned(rx), sy = 1 + unsigned(ry);
                            const auto texel = test_sprite_draw::texel(sx / 4, sx % 4, sy);
                            constexpr double clear[]{8. / 255, 16. / 255, 32. / 255, 1};
                            for (unsigned lane = 0; lane < 4; ++lane) {
                                auto expected = texel[lane] / 255.;
                                const auto opacity = texel[3] / 255.;
                                if (alpha == MaterialAlpha::Mask && opacity < .5)
                                    expected = clear[lane];
                                else if (alpha == MaterialAlpha::Blend)
                                    expected = (lane == 3 ? opacity : expected * opacity) +
                                               clear[lane] * (1 - opacity);
                                else if (lane == 3)
                                    expected = 1;
                                if (std::abs(int(pixels[y * 64 + x][lane]) -
                                             int(std::lround(expected * 255))) > 1)
                                    throw std::runtime_error(
                                        "Cropped Sprite pixels differ from independent image "
                                        "sampling: "
                                        "mode=" +
                                        std::to_string(mode) + " flip_x=" + std::to_string(flip_x) +
                                        " flip_y=" + std::to_string(flip_y) +
                                        " alpha=" + std::to_string(unsigned(alpha)) +
                                        " x=" + std::to_string(x) + " y=" + std::to_string(y) +
                                        " rx=" + std::to_string(rx) + " ry=" + std::to_string(ry) +
                                        " lane=" + std::to_string(lane) +
                                        " actual=" + std::to_string(pixels[y * 64 + x][lane]) +
                                        " expected=" + std::to_string(std::lround(expected * 255)));
                            }
                        }
                }
    }
}
} // namespace forge::test_sprite_bundle
