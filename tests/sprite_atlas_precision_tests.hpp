#pragma once
#include "display_resolve.hpp"
#include "sprite_draw_render_tests.hpp"

namespace forge::test_sprite_atlas_precision {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    const auto mesh = asset_detail::engine_mesh_resource(engine_primitive(8));
    const auto native_mesh = upload_mesh(device, mesh.mesh);
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE atlas precision HDR target";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Atlas precision targets unavailable");
    DisplayResolve display(presentation);
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 1;
    AffineTransform camera_world;
    camera_world.m[11] = -2;
    const auto view = camera_view(camera, camera_world, 64, 64);
    for (const auto [width, pixel] :
         {std::pair{16383u, 16374u}, std::pair{16383u, 8199u}, std::pair{8191u, 8188u}}) {
        TextureData source;
        source.width = width;
        source.height = 1;
        source.alpha = TextureAlpha::Opaque;
        source.subresources = {std::vector<std::byte>(width * 4, std::byte{255})};
        for (unsigned lane = 0; lane < 3; ++lane)
            source.subresources[0][pixel * 4 + lane] = std::byte{0};
        const auto texture = upload_texture(device, source);
        MeshDraw::Textures textures;
        textures["baseColorTexture"] = texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        MaterialData material;
        material.model = "forge.gltf.unlit.v1";
        material.double_sided = true;
        material.depth_test = material.depth_write = false;
        material.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        MeshDraw draw(presentation, context, native_mesh.lods[0].parts[0], material, textures,
                      TEX_FORMAT_RGBA16_FLOAT, TEX_FORMAT_D32_FLOAT, false, SHADER_COMPILER_DEFAULT,
                      SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr, MeshDrawProfile::Sprite);
        const SpriteDrawValues values{sprite_region_mapping({pixel, 0, 1, 1}, width, 1)};
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        constexpr float clear[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport viewport{0, 0, 64, 64, 0, 1};
        context->SetViewports(1, &viewport, 64, 64);
        draw.draw(context, {}, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, nullptr, {},
                  {}, {}, &values);
        // Valid exposure must not turn a neighboring atlas texel into a bright
        // edge around a mathematically black, single-pixel frame.
        auto* output =
            display.resolve(context, color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 8);
        presentation.end_frame(context);
        const auto pixels = readback(output->GetTexture());
        context->FinishFrame();
        device->ReleaseStaleResources();
        unsigned maximum = 0;
        for (const auto& value : pixels)
            maximum =
                std::max({maximum, unsigned(value[0]), unsigned(value[1]), unsigned(value[2])});
        if (maximum != 0)
            throw std::runtime_error("Atlas neighboring white texel leaked into an isolated black "
                                     "frame through HDR/display: width=" +
                                     std::to_string(width) + " pixel=" + std::to_string(pixel) +
                                     " maxRGB=" + std::to_string(maximum));
    }
}
} // namespace forge::test_sprite_atlas_precision
