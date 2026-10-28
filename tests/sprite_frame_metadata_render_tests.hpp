#pragma once
#include "sprite_atlas_packing.hpp"
#include "sprite_draw_render_tests.hpp"
#include "sprite_render_values.hpp"

namespace forge::test_sprite_frame_metadata_render {
inline TextureData original(unsigned profile) {
    TextureData image;
    image.width = profile == 3 ? 16383 : profile == 0 ? 4 : 13;
    image.height = profile == 0 || profile == 3 ? 7 : 11;
    image.alpha = TextureAlpha::Straight;
    image.subresources.emplace_back(std::size_t(image.width) * image.height * 4);
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x) {
            const auto at = (std::size_t(y) * image.width + x) * 4;
            image.subresources[0][at] = std::byte(19 + x * 13);
            image.subresources[0][at + 1] = std::byte(31 + y * 17);
            image.subresources[0][at + 2] = std::byte(231 - x * 7 - y * 3);
            image.subresources[0][at + 3] =
                profile == 2 || (profile == 1 && (x < 4 || x > 5 || y < 3 || y > 7)) ||
                        (profile == 3 && (x < 8190 || x > 8192 || y < 2 || y > 4))
                    ? std::byte{}
                    : std::byte(64 + (x * 31 + y * 17) % 192);
        }
    return image;
}
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    const auto mesh = asset_detail::engine_mesh_resource(engine_primitive(8));
    const auto gpu_mesh = upload_mesh(device, mesh.mesh);
    TextureDesc desc;
    desc.Name = "FORGE logical Sprite canvas proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Logical Sprite canvas targets unavailable");
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 1;
    AffineTransform camera_world;
    camera_world.m[11] = -2;
    const auto view = camera_view(camera, camera_world, 64, 64);
    const std::array<float, 4> background{8.f / 255, 16.f / 255, 32.f / 255, 1};
    for (unsigned profile = 0; profile < 4; ++profile)
        for (bool rotation : {false, true}) {
            const auto source = original(profile);
            auto companion = original(0);
            companion.width = 7;
            companion.height = 4;
            const std::array<const TextureData*, 2> inputs{&source, &companion};
            const auto packed =
                build_sprite_atlas(inputs, {16, 1, profile == 3 ? 1024u * 1024u : 16384u, true,
                                            profile != 0, rotation});
            check(profile >= 2 || packed.layout.frames[0].rotated_clockwise == rotation,
                  "Pixel fixture did not exercise the requested stored orientation");
            const auto original_texture = upload_texture(device, source);
            const auto packed_texture = upload_texture(device, packed.texture);
            for (bool linear : {false, true}) {
                MaterialData material;
                material.model = "forge.gltf.unlit.v1";
                material.alpha = MaterialAlpha::Blend;
                material.double_sided = true;
                material.depth_test = material.depth_write = false;
                auto& slot = material.textures["baseColorTexture"];
                slot.semantic = TextureSemantic::Color;
                slot.sampler.min = slot.sampler.mag =
                    linear ? TextureFilter::Linear : TextureFilter::Nearest;
                MeshDraw::Textures original_bindings, packed_bindings;
                original_bindings["baseColorTexture"] =
                    original_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                packed_bindings["baseColorTexture"] =
                    packed_texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                MeshDraw original_draw(
                    presentation, context, gpu_mesh.lods[0].parts[0], material, original_bindings,
                    TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, true, SHADER_COMPILER_DEFAULT,
                    SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr, MeshDrawProfile::Sprite);
                MeshDraw packed_draw(
                    presentation, context, gpu_mesh.lods[0].parts[0], material, packed_bindings,
                    TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, true, SHADER_COMPILER_DEFAULT,
                    SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr, MeshDrawProfile::Sprite);
                for (bool flip_x : {false, true})
                    for (bool flip_y : {false, true})
                        for (bool instanced : {false, true})
                            for (unsigned layout = 0; layout < 5; ++layout) {
                                SpriteRenderer sprite;
                                sprite.image_mode = layout == 0 ? 0 : (layout % 2 ? 1 : 2);
                                sprite.pixels_per_meter = layout < 3 ? 17 : 2;
                                sprite.border_left = 1;
                                sprite.border_right = 2;
                                sprite.border_top = 2;
                                sprite.border_bottom = 1;
                                sprite.width = .8;
                                sprite.height = .7;
                                if (profile == 3 && layout == 1) {
                                    // Three original center pixels in a16383px
                                    // canvas become a visible stretched center.
                                    // This exercises cancellation near large trim
                                    // offsets instead of accepting a blank image.
                                    sprite.border_left = sprite.border_right = 8190;
                                    sprite.pixels_per_meter = 32766 / sprite.width;
                                }
                                sprite.pivot_x = .35;
                                sprite.pivot_y = .6;
                                sprite.flip_x = flip_x;
                                sprite.flip_y = flip_y;
                                const auto baseline_values = sprite_draw_values(sprite, source);
                                const auto world = sprite_world_transform(sprite, {});
                                sprite.frame_regions = {packed.layout.frames[0]};
                                const auto packed_values =
                                    sprite_draw_values(sprite, packed.texture);
                                const auto render = [&](MeshDraw& draw,
                                                        const SpriteDrawValues& values) {
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
                                    auto second_values = values;
                                    second_values.image.mode = values.image.mode == 2 ? 1 : 2;
                                    second_values.image.axes[0].repeat = 2.35;
                                    second_values.image.axes[1].repeat = 1.6;
                                    second_values.color = {.4f, 1, 1, .7f};
                                    auto second_world = world;
                                    second_world.m[3] += .23;
                                    second_world.m[7] -= .12;
                                    const std::array<MeshDraw::Instance, 2> instances{
                                        MeshDraw::Instance{world, {}, &values},
                                        MeshDraw::Instance{second_world, {}, &second_values}};
                                    draw.draw(context, world, view, {}, nullptr, nullptr, nullptr,
                                              {}, nullptr, {}, nullptr,
                                              instanced ? std::span(instances)
                                                        : std::span<const MeshDraw::Instance>{},
                                              {}, {}, instanced ? nullptr : &values);
                                    presentation.end_frame(context);
                                    context->Flush();
                                    auto pixels = readback(color);
                                    context->FinishFrame();
                                    device->ReleaseStaleResources();
                                    return pixels;
                                };
                                const auto baseline = render(original_draw, baseline_values);
                                const auto actual = render(packed_draw, packed_values);
                                if (profile == 3 && layout == 1)
                                    check(std::any_of(baseline.begin(), baseline.end(),
                                                      [](const auto& pixel) {
                                                          return pixel[0] > 40 || pixel[1] > 40;
                                                      }),
                                          "Large logical-canvas sliced reference contains no "
                                          "visible center");
                                check(actual.size() == baseline.size(),
                                      "Frame metadata readback extent changed");
                                for (std::size_t pixel = 0; pixel < actual.size(); ++pixel)
                                    for (unsigned lane = 0; lane < 4; ++lane)
                                        if (std::abs(int(actual[pixel][lane]) -
                                                     int(baseline[pixel][lane])) > 2)
                                            throw std::runtime_error(
                                                "Packed logical-canvas pixels differ from the "
                                                "original: profile=" +
                                                std::to_string(profile) +
                                                " rotation=" + std::to_string(rotation) +
                                                " linear=" + std::to_string(linear) + " flip=" +
                                                std::to_string(flip_x) + std::to_string(flip_y) +
                                                " instance=" + std::to_string(instanced) +
                                                " layout=" + std::to_string(layout) +
                                                " pixel=" + std::to_string(pixel));
                            }
            }
        }
}
} // namespace forge::test_sprite_frame_metadata_render
