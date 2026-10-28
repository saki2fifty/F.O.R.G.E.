#pragma once
#include "sprite_draw_render_tests.hpp"
#include <forge/scene_render_settings.hpp>
#include <iostream>

namespace forge::test_distance_fog {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    const auto mesh = asset_detail::engine_mesh_resource(engine_primitive(8));
    const auto gpu_mesh = upload_mesh(device, mesh.mesh);
    TextureData white_source;
    white_source.width = white_source.height = 1;
    white_source.alpha = TextureAlpha::Opaque;
    white_source.subresources = {{std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}};
    const auto white = upload_texture(device, white_source);
    MeshDraw::Textures textures;
    textures["baseColorTexture"] = white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE radial distance fog oracle";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Distance fog targets unavailable");
    constexpr std::array<float, 4> background{.02f, .05f, .09f, 1};
    constexpr std::array<float, 3> base{.8f, .2f, .1f};
    const SceneDistanceFog fog{true, {.1f, .4f, .7f}, 1, 4};
    auto disabled = fog;
    disabled.enabled = false;
    std::uint64_t pixels_checked = 0;
    for (const auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Blend, MaterialAlpha::Mask}) {
        MaterialData material;
        material.model = "forge.gltf.unlit.v1";
        material.alpha = alpha;
        material.double_sided = true;
        material.depth_test = material.depth_write = false;
        material.alpha_cutoff = .5f;
        material.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                  {base[0], base[1], base[2], .25f}};
        for (const bool sprite : {false, true}) {
            MeshDraw draw(presentation, context, gpu_mesh.lods[0].parts[0], material, textures,
                          TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, true,
                          SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                          sprite ? MeshDrawProfile::Sprite : MeshDrawProfile::Mesh);
            SpriteDrawValues sprite_values;
            sprite_values.region = sprite_region_mapping({0, 0, 1, 1}, 1, 1);
            for (const unsigned projection : {0u, 1u})
                for (const double origin : {0., 1e12})
                    for (const double distance : {.25, 1., 2.5, 4., 6.})
                        for (const unsigned instances : {0u, 2u}) {
                            Camera camera;
                            camera.projection = projection;
                            camera.orthographic_height = 4;
                            AffineTransform camera_world;
                            camera_world.m[3] = camera_world.m[7] = camera_world.m[11] = origin;
                            const auto view = camera_view(camera, camera_world, 64, 64);
                            AffineTransform world;
                            world.m[0] = world.m[5] = 16;
                            world.m[3] = world.m[7] = origin;
                            world.m[11] = origin + distance;
                            const auto* values = sprite ? &sprite_values : nullptr;
                            const std::array batch{MeshDraw::Instance{world, {}, values},
                                                   MeshDraw::Instance{world, {}, values}};
                            const auto render = [&](const SceneDistanceFog* selected) {
                                presentation.begin_frame();
                                auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                                context->SetRenderTargets(
                                    1, &rtv, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                                context->ClearRenderTarget(
                                    rtv, background.data(),
                                    RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                                const Diligent::Viewport viewport{0, 0, 64, 64, 0, 1};
                                context->SetViewports(1, &viewport, 64, 64);
                                draw.draw(context, world, view, {}, nullptr, nullptr, nullptr, {},
                                          nullptr, {}, nullptr,
                                          instances ? std::span<const MeshDraw::Instance>(batch)
                                                    : std::span<const MeshDraw::Instance>{},
                                          {}, {}, values, selected);
                                presentation.end_frame(context);
                                auto pixels = readback(color);
                                context->FinishFrame();
                                device->ReleaseStaleResources();
                                return pixels;
                            };
                            const auto before = render(nullptr);
                            const auto actual = render(&fog);
                            check(render(&disabled) == before,
                                  "Disabled fog changed pixels or retained the previous fog value");
                            for (unsigned y = 0; y < 64; ++y)
                                for (unsigned x = 0; x < 64; ++x) {
                                    const double scale = projection ? 1. : distance;
                                    const auto dx =
                                        (2 * (x + .5) / 64 - 1) * scale / view.projection[0];
                                    const auto dy =
                                        (2 * (y + .5) / 64 - 1) * scale / view.projection[5];
                                    const auto radial = std::hypot(dx, dy, distance);
                                    const auto amount = std::clamp((radial - 1) / 3, 0., 1.);
                                    for (unsigned lane = 0; lane < 4; ++lane) {
                                        double expected = background[lane];
                                        if (alpha != MaterialAlpha::Mask && lane < 3) {
                                            const auto source =
                                                std::lerp(double(base[lane]),
                                                          double(fog.color[lane]), amount);
                                            const double opacity =
                                                alpha == MaterialAlpha::Blend ? .25 : 1;
                                            for (unsigned pass = 0;
                                                 pass < (instances ? instances : 1); ++pass)
                                                expected =
                                                    source * opacity + expected * (1 - opacity);
                                        }
                                        if (std::abs(int(actual[y * 64 + x][lane]) -
                                                     int(std::lround(expected * 255))) > 2)
                                            throw std::runtime_error(
                                                "Fog oracle mismatch " +
                                                std::to_string(projection) + ":" +
                                                std::to_string(sprite) + ":" +
                                                std::to_string(origin) + ":" +
                                                std::to_string(distance) + ":" +
                                                std::to_string(instances) + " pixel " +
                                                std::to_string(x) + "," + std::to_string(y));
                                    }
                                    ++pixels_checked;
                                }
                        }
        }
    }
    std::cout
        << "Radial fog: " << pixels_checked
        << " pixels, both projections/origins, Mesh/Sprite/direct/instances/alpha/disabled PASS\n";
}
} // namespace forge::test_distance_fog
