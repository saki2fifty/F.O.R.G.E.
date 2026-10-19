#pragma once
#include "mesh_render_host.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/render_view.hpp>
#include <thread>

namespace forge::test_sprite_projection {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& root) {
    using namespace Diligent;
    using test_sprite_draw::check;
    std::filesystem::create_directories(root);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialResourceData surface;
    surface.values.model = "forge.gltf.unlit.v1";
    surface.values.alpha = MaterialAlpha::Blend;
    surface.values.double_sided = true;
    surface.values.depth_test = surface.values.depth_write = false;
    auto& slot = surface.values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = TextureFilter::Nearest;
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(material, surface);
    MeshSceneRenderer renderer(host);
    RenderScene scene;
    scene.scene = AssetId::generate();
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = material;
    sprite.renderer.columns = sprite.renderer.rows = 4;
    sprite.renderer.width = 2;
    sprite.renderer.height = 1;
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 4;
    constexpr std::array<std::uint8_t, 4> background{8, 16, 32, 255}, ink{255, 0, 255, 255};
    const std::array<float, 4> clear{8.f / 255, 16.f / 255, 32.f / 255, 1};
    auto* device = presentation.device();
    for (const auto size : {std::pair{96u, 48u}, {48u, 96u}, {127u, 73u}, {73u, 127u}}) {
        const auto [width, height] = size;
        RefCntAutoPtr<ITexture> color, depth;
        TextureDesc desc;
        desc.Name = "FORGE Sprite projection oracle";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device->CreateTexture(desc, nullptr, &color);
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(desc, nullptr, &depth);
        check(color && depth, "Sprite projection targets unavailable");
        const auto view = camera_view(camera, {}, width, height);
        for (unsigned pose = 0; pose < 4; ++pose) {
            sprite.renderer.pivot_x = sprite.renderer.pivot_y = pose == 1 ? 0 : .5;
            sprite.world.m =
                pose == 2 ? std::array<double, 12>{.25, -1.2, 0, .35, -.8, -.2, 0, -.15, 0, 0, 1, 3}
                : pose == 3 ? std::array<double, 12>{-1, 0, 0, .6, 0, -1, 0, .3, 0, 0, 1, 3}
                            : std::array<double, 12>{1, 0, 0, -.5, 0, 1, 0, .4, 0, 0, 1, 3};
            scene.sprites = {sprite};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            do {
                renderer.update(scene);
                check(std::chrono::steady_clock::now() < deadline,
                      "Sprite projection resources timed out");
                if (renderer.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (renderer.pending());
            presentation.begin_frame();
            auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearRenderTarget(rtv, clear.data(),
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const Diligent::Viewport viewport{0, 0, float(width), float(height), 0, 1};
            context->SetViewports(1, &viewport, width, height);
            renderer.draw(scene, view, UINT32_MAX, color, dsv);
            presentation.end_frame(context);
            host->submit();
            const auto pixels = readback(color);
            context->FinishFrame();
            device->ReleaseStaleResources();
            check(renderer.diagnostics().empty(),
                  "Sprite projection produced resource diagnostics");
            // Independent XY polygon and orthographic inverse. Neither the Sprite
            // adapter nor the renderer's projection/bounds helpers form this oracle.
            std::array<std::array<double, 2>, 4> polygon;
            unsigned corner = 0;
            for (const auto uv : {std::pair{0., 0.}, {1., 0.}, {1., 1.}, {0., 1.}}) {
                const auto x = (uv.first - sprite.renderer.pivot_x) * sprite.renderer.width;
                const auto y = (sprite.renderer.pivot_y - uv.second) * sprite.renderer.height;
                const auto& m = sprite.world.m;
                polygon[corner++] = {m[0] * x + m[1] * y + m[3], m[4] * x + m[5] * y + m[7]};
            }
            unsigned inside_count = 0, outside_count = 0;
            const double camera_width = 4. * width / height;
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const double px = ((x + .5) / width - .5) * camera_width;
                    const double py = (.5 - (y + .5) / height) * 4.;
                    bool positive = false, negative = false, boundary = false;
                    for (unsigned edge = 0; edge < 4; ++edge) {
                        const auto a = polygon[edge], b = polygon[(edge + 1) % 4];
                        const auto cross =
                            (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
                        boundary |= std::abs(cross) < 1e-5 * std::hypot(b[0] - a[0], b[1] - a[1]);
                        positive |= cross > 0;
                        negative |= cross < 0;
                    }
                    if (boundary)
                        continue; // Raster top-left rule at an exact edge is not this oracle's
                                  // subject.
                    const bool inside = !(positive && negative);
                    inside ? ++inside_count : ++outside_count;
                    check(pixels[std::size_t(y) * width + x] == (inside ? ink : background),
                          "Sprite pixels differ from independent aspect/pivot/reflection polygon");
                }
            check(inside_count && outside_count,
                  "Sprite projection oracle exercised no inside/outside pixels");
            double center_x = 0, center_y = 0;
            for (const auto p : polygon) {
                center_x += p[0] / 4;
                center_y += p[1] / 4;
            }
            check(renderer.pick(scene, view, (center_x / camera_width + .5) * width,
                                (.5 - center_y / 4) * height) == sprite.entity,
                  "Sprite geometric picking disagrees with its projected pivot/reflected pose");
        }
    }
}
} // namespace forge::test_sprite_projection
