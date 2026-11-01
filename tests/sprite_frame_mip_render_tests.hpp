#pragma once
#include "mesh_render_host.hpp"
#include "sprite_draw_render_tests.hpp"
#include "sprite_frame_mips.hpp"
#include "sprite_render_values.hpp"
#include <chrono>
#include <thread>

namespace forge::test_sprite_frame_mip_render {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& directory,
              const MaterialShaderSnapshot* custom_surface = nullptr) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    const auto resources = test_sprite_draw::image_resources(device, nullptr, custom_surface);
    const auto source_mesh = asset_detail::engine_mesh_resource(engine_primitive(8));
    const auto mesh = upload_mesh(device, source_mesh.mesh);
    TextureDesc desc;
    desc.Name = "FORGE isolated Sprite mip filtering proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Sprite mip targets unavailable");
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 1;
    AffineTransform camera_world;
    camera_world.m[11] = -2;
    const auto view = camera_view(camera, camera_world, 64, 64);
    const auto render = [&](MeshDraw& draw, ITextureView* image, const SpriteDrawValues& values,
                            const AffineTransform& world) {
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, 64, 64, 0, 1};
        context->SetViewports(1, &viewport, 64, 64);
        draw.bind_sprite_mip(image);
        draw.draw(context, world, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, nullptr, {},
                  {}, {}, &values);
        presentation.end_frame(context);
        auto pixels = readback(color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    std::size_t checked = 0;
    for (unsigned pattern = 0; pattern < 3; ++pattern) {
        TextureData atlas;
        atlas.width = 64;
        atlas.height = 32;
        atlas.alpha = TextureAlpha::Straight;
        atlas.format = pattern == 2 ? TextureFormat::RGBA8Srgb : TextureFormat::RGBA8;
        auto& pixels = atlas.subresources.emplace_back(64 * 32 * 4);
        for (unsigned y = 0; y < 32; ++y)
            for (unsigned x = 0; x < 64; ++x) {
                const auto at = (y * 64 + x) * 4;
                const bool odd = (x + y) % 2;
                const unsigned intensity = pattern == 2 ? 128 : 255;
                pixels[at] = std::byte(x < 32 && !odd ? intensity : 0);
                pixels[at + 1] = std::byte(x >= 32 || (pattern == 1 && odd) ? 255 : 0);
                pixels[at + 2] = std::byte(x < 32 && odd && pattern != 1 ? intensity : 0);
                pixels[at + 3] = std::byte(pattern == 1 && x < 32 && odd ? 0 : 255);
            }
        SpriteRenderer sprite;
        sprite.columns = 2;
        const auto plan = sprite_frame_mip_plan(sprite, atlas, 100000);
        const auto image = prepare_sprite_frame_mips(atlas, plan.frames[0], 100000);
        const auto gpu = upload_texture(device, image);
        auto material = test_sprite_draw::image_material(custom_surface);
        material.alpha = pattern == 1 ? MaterialAlpha::Blend : MaterialAlpha::Opaque;
        material.double_sided = true;
        material.depth_test = material.depth_write = false;
        material.textures[test_sprite_draw::image_key(custom_surface)].semantic =
            TextureSemantic::Color;
        for (unsigned filter : {1u, 2u}) {
            MeshDraw draw(presentation, context, mesh.lods[0].parts[0], material,
                          resources.bindings, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, false,
                          SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT,
                          custom_surface, sprite_draw_profile(filter));
            for (bool flip_x : {false, true})
                for (bool flip_y : {false, true}) {
                    sprite.flip_x = flip_x;
                    sprite.flip_y = flip_y;
                    AffineTransform world;
                    world.m[0] = world.m[5] = .125;
                    const auto values = sprite_canvas_draw_values(sprite, 32, 32);
                    const auto output = render(
                        draw, gpu->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), values, world);
                    const int average = pattern == 2 ? 28 : 128;
                    const std::array<int, 4> expected{average, 0, pattern == 1 ? 0 : average, 255};
                    for (unsigned y = 28; y < 36; ++y)
                        for (unsigned x = 28; x < 36; ++x)
                            for (unsigned lane = 0; lane < 4; ++lane) {
                                check(std::abs(int(output[y * 64 + x][lane]) - expected[lane]) <= 1,
                                      "Minified frame mixed its neighbor, invisible RGB, or sRGB");
                                ++checked;
                            }
                }
        }
    }
    std::cout << "Independent Sprite minification samples checked: " << checked << '\n';
    TextureData original;
    original.width = 32;
    original.height = 16;
    original.alpha = TextureAlpha::Straight;
    original.format = TextureFormat::RGBA8Srgb;
    original.subresources.emplace_back(32 * 16 * 4, std::byte{0});
    TextureData packed;
    packed.width = 64;
    packed.height = 32;
    packed.alpha = TextureAlpha::Straight;
    packed.format = original.format;
    packed.subresources.emplace_back(64 * 32 * 4, std::byte{255});
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 16; ++x) {
            const std::array<std::byte, 4> pixel{std::byte(32 + x * 13), std::byte(17 + y * 29),
                                                 std::byte(231 - x * 7 - y * 3),
                                                 std::byte(64 + (x * 31 + y * 17) % 192)};
            const auto logical = ((y + 4) * 32 + x + 8) * 4;
            const auto stored = ((5 + x) * 64 + 3 + 7 - y) * 4;
            std::copy(pixel.begin(), pixel.end(), original.subresources[0].begin() + logical);
            std::copy(pixel.begin(), pixel.end(), packed.subresources[0].begin() + stored);
        }
    SpriteRenderer logical_sprite, packed_sprite;
    packed_sprite.frame_regions = {{3, 5, 8, 16, true, 32, 16, 8, 4}};
    const auto original_plan = sprite_frame_mip_plan(logical_sprite, original, 100000);
    const auto packed_plan = sprite_frame_mip_plan(packed_sprite, packed, 100000);
    const auto original_mips = prepare_sprite_frame_mips(original, original_plan.frames[0], 100000);
    const auto packed_mips = prepare_sprite_frame_mips(packed, packed_plan.frames[0], 100000);
    check(original_mips.subresources == packed_mips.subresources,
          "Trimmed clockwise frame filtering differs from its full isolated image");
    const auto original_gpu = upload_texture(device, original_mips);
    const auto packed_gpu = upload_texture(device, packed_mips);
    auto translucent = test_sprite_draw::image_material(custom_surface);
    translucent.alpha = MaterialAlpha::Blend;
    translucent.double_sided = true;
    translucent.depth_test = translucent.depth_write = false;
    auto& base = translucent.textures[test_sprite_draw::image_key(custom_surface)];
    base.semantic = TextureSemantic::Color;
    base.sampler.min = base.sampler.mag = TextureFilter::Linear;
    std::size_t canvas_cases = 0;
    for (unsigned filter : {1u, 2u}) {
        MeshDraw draw(presentation, context, mesh.lods[0].parts[0], translucent, resources.bindings,
                      TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, false, SHADER_COMPILER_DEFAULT,
                      SHADER_OPTIMIZATION_LEVEL_DEFAULT, custom_surface,
                      sprite_draw_profile(filter));
        for (unsigned mode : {0u, 1u, 2u})
            for (bool flip_x : {false, true})
                for (bool flip_y : {false, true}) {
                    packed_sprite.image_mode = logical_sprite.image_mode = mode;
                    packed_sprite.flip_x = logical_sprite.flip_x = flip_x;
                    packed_sprite.flip_y = logical_sprite.flip_y = flip_y;
                    packed_sprite.pixels_per_meter = logical_sprite.pixels_per_meter = 64;
                    packed_sprite.border_left = logical_sprite.border_left = 4;
                    packed_sprite.border_right = logical_sprite.border_right = 6;
                    packed_sprite.border_top = logical_sprite.border_top = 2;
                    packed_sprite.border_bottom = logical_sprite.border_bottom = 3;
                    const auto logical = sprite_canvas_draw_values(logical_sprite, 32, 16);
                    const auto stored = sprite_canvas_draw_values(packed_sprite, 32, 16);
                    AffineTransform world;
                    world.m[0] = .23;
                    world.m[5] = .19;
                    const auto expected =
                        render(draw, original_gpu->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                               logical, world);
                    const auto actual =
                        render(draw, packed_gpu->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                               stored, world);
                    check(
                        actual == expected,
                        "Filtered logical canvas changed fill, flips, trim, or stored orientation");
                    ++canvas_cases;
                }
    }
    std::cout << "Filtered trimmed/clockwise canvas comparisons: " << canvas_cases << '\n';
    if (custom_surface)
        return; // Direct program checks above; publication has its own fixture.

    // Exercise the real asynchronous selection/adoption owner with more unique
    // frames than its shared worker queue can admit at once.
    std::filesystem::create_directories(directory);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, directory, true);
    auto catalog = std::make_shared<const AssetCatalog>(directory);
    const auto catalog_before = catalog->document();
    host->catalog(catalog);
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialResourceData surface;
    surface.values.model = "forge.gltf.unlit.v1";
    surface.values.double_sided = true;
    surface.values.depth_test = surface.values.depth_write = false;
    surface.values.alpha = MaterialAlpha::Blend;
    surface.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    surface.values.textures["baseColorTexture"].sampler.min = TextureFilter::Nearest;
    surface.values.textures["baseColorTexture"].sampler.mag = TextureFilter::Nearest;
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(material, surface);
    MeshSceneRenderer renderer(host);
    RenderScene scene;
    scene.scene = AssetId::generate();
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = material;
    sprite.renderer.mip_filter = 1;
    for (unsigned width = 1; width <= 128; ++width)
        sprite.renderer.frame_regions.push_back({0, 0, 1, 1, false, width, 1});
    scene.sprites = {sprite};
    const auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            renderer.update(scene);
            if (!renderer.pending())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Sprite mip bank did not settle");
    };
    const auto render_scene = [&] {
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, 64, 64, 0, 1};
        context->SetViewports(1, &viewport, 64, 64);
        renderer.draw(scene, view, UINT32_MAX, color, dsv);
        presentation.end_frame(context);
        host->submit();
        auto pixels = readback(color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    settle();
    check(renderer.diagnostics().empty() && renderer.bundle_count() == 1 &&
              host->inspect().gpu_textures.resident == 128,
          "Complete mip bank did not incrementally admit128 frames without an atlas GPU copy");
    const auto magenta = render_scene();
    check(std::all_of(magenta.begin(), magenta.end(),
                      [](const auto& pixel) {
                          return pixel == std::array<std::uint8_t, 4>{255, 0, 255, 255};
                      }),
          "Cold Sprite mip bank did not draw its selected frame");
    scene.sprites.push_back(scene.sprites[0]);
    scene.sprites[1].entity = EntityId::generate();
    settle();
    (void)render_scene();
    check(renderer.draw_stats().calls == 1 && renderer.draw_stats().instances == 2,
          "Sprites selecting the same filtered frame lost consecutive instancing");
    scene.sprites[1].renderer.frame = 1;
    settle();
    (void)render_scene();
    check(renderer.draw_stats().calls == 2 && renderer.draw_stats().instances == 2,
          "Sprite batching mixed distinct filtered frame views");
    scene.sprites.resize(1);
    scene.sprites[0].renderer.mip_filter = 2;
    settle();
    check(render_scene() == magenta && renderer.diagnostics().empty(),
          "Changing the mip filter failed to reuse the complete frame pixels");
    scene.sprites[0].renderer.frame_regions = {{0, 0, 1, 1}, {3, 3, 1, 1}};
    settle();
    check(render_scene() == magenta && renderer.diagnostics().empty(),
          "Replacement frame bank did not become current");
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::White);
    host->preview_material(material, surface);
    settle();
    check(!renderer.diagnostics().empty() && render_scene() == magenta,
          "Invalid unused frame replaced the complete last-good source and values");
    scene.sprites[0].renderer.frame_regions.resize(1);
    settle();
    const auto white = render_scene();
    check(renderer.diagnostics().empty() &&
              std::all_of(white.begin(), white.end(),
                          [](const auto& pixel) {
                              return pixel == std::array<std::uint8_t, 4>{255, 255, 255, 255};
                          }),
          "Corrected bank did not adopt its new texture revision");
    scene.sprites[0].renderer.mip_filter = 0;
    settle();
    check(render_scene() == white && renderer.diagnostics().empty() &&
              catalog->document() == catalog_before,
          "Disabling filtered frames changed pixels or mutated authoritative assets");
}
} // namespace forge::test_sprite_frame_mip_render
