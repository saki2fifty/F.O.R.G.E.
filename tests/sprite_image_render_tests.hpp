#pragma once
#include "sprite_draw_render_tests.hpp"
#include "sprite_render_values.hpp"

namespace forge::test_sprite_image {
inline std::array<std::uint8_t, 4> texel(unsigned x, unsigned y) {
    // Original asymmetric pixel art: distinct corner colors and a translucent
    // striped center. Adjacent atlas cells are deliberately unrelated colors.
    const bool left = x < 2, right = x >= 10, top = y < 1, bottom = y >= 7;
    if (left || right || top || bottom)
        return {std::uint8_t(left    ? 255
                             : right ? 24
                                     : 120),
                std::uint8_t(top      ? 255
                             : bottom ? 24
                                      : 120),
                std::uint8_t((x + y) % 2 ? 60 : 220), 255};
    return {std::uint8_t(29 + x * 13), std::uint8_t(17 + y * 21),
            std::uint8_t((x + y) % 2 ? 40 : 180), 96};
}
inline double source_pixel(unsigned mode, double fraction, double meters, double density,
                           unsigned pixels, unsigned low, unsigned high, bool flipped) {
    const double u = flipped ? 1 - fraction : fraction;
    if (mode == 0)
        return u * pixels;
    const double extent = meters * density, position = u * extent;
    if (mode == 2)
        return std::fmod(position, double(pixels));
    // Independent pixel-space oracle, not the packed shader's normalized spans.
    const double compression = low + high ? std::min(1., extent / (low + high)) : 1.;
    const double first = low * compression, last = extent - high * compression;
    if (first > 0 && position <= first)
        return position / compression;
    if (last < extent && position >= last)
        return pixels - (extent - position) / compression;
    if (last <= first)
        return low;
    return low + (position - first) * (pixels - low - high) / (last - first);
}
inline std::array<double, 4> filtered(double px, double py, bool linear) {
    std::array<double, 4> result{};
    if (!linear) {
        const auto p = texel(std::min(12u, unsigned(px)), std::min(8u, unsigned(py)));
        for (unsigned lane = 0; lane < 4; ++lane)
            result[lane] = p[lane] / 255.;
        return result;
    }
    const auto x = std::clamp(px - .5, 0., 12.), y = std::clamp(py - .5, 0., 8.);
    const auto x0 = unsigned(x), y0 = unsigned(y);
    const auto a = texel(x0, y0), b = texel(std::min(12u, x0 + 1), y0);
    const auto c = texel(x0, std::min(8u, y0 + 1)),
               d = texel(std::min(12u, x0 + 1), std::min(8u, y0 + 1));
    for (unsigned lane = 0; lane < 4; ++lane)
        result[lane] = std::lerp(std::lerp(double(a[lane]), double(b[lane]), x - x0),
                                 std::lerp(double(c[lane]), double(d[lane]), x - x0), y - y0) /
                       255.;
    return result;
}
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& images) {
    using namespace Diligent;
    using test_sprite_draw::check;
    auto* device = presentation.device();
    std::filesystem::create_directories(images);
    const auto mesh = asset_detail::engine_mesh_resource(engine_primitive(8));
    const auto gpu_mesh = upload_mesh(device, mesh.mesh);
    TextureData source;
    source.width = 32;
    source.height = 24;
    source.alpha = TextureAlpha::Straight;
    source.subresources.resize(1);
    for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 32; ++x) {
            const auto pixel = x >= 17 && x < 30 && y >= 13 && y < 22
                                   ? texel(x - 17, y - 13)
                                   : std::array<std::uint8_t, 4>{255, 0, 255, 255};
            for (auto lane : pixel)
                source.subresources[0].push_back(std::byte(lane));
        }
    const auto atlas = upload_texture(device, source);
    MeshDraw::Textures textures;
    textures["baseColorTexture"] = atlas->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE original sliced/repeated Sprite oracle";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 128;
    desc.Height = 96;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth && atlas, "Image fill oracle resources unavailable");
    const std::array<float, 4> background{8.f / 255, 16.f / 255, 32.f / 255, 0};
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    AffineTransform camera_world;
    camera_world.m[11] = -2;
    const auto begin = [&](bool clear) {
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &rtv, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (clear)
            context->ClearRenderTarget(rtv, background.data(),
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport viewport{0, 0, 128, 96, 0, 1};
        context->SetViewports(1, &viewport, 128, 96);
    };
    const auto finish = [&] {
        presentation.end_frame(context);
        context->Flush();
        auto pixels = readback(color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    unsigned cases = 0;
    std::uint64_t checked = 0, excluded = 0;
    for (bool linear : {false, true})
        for (auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Blend, MaterialAlpha::Mask}) {
            MaterialData material;
            material.model = "forge.gltf.unlit.v1";
            material.double_sided = true;
            material.depth_test = material.depth_write = false;
            material.alpha = alpha;
            auto& slot = material.textures["baseColorTexture"];
            slot.semantic = TextureSemantic::Color;
            slot.sampler.min = slot.sampler.mag =
                linear ? TextureFilter::Linear : TextureFilter::Nearest;
            MeshDraw draw(presentation, context, gpu_mesh.lods[0].parts[0], material, textures,
                          TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, true,
                          SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                          MeshDrawProfile::Sprite);
            for (unsigned mode = 0; mode < 3; ++mode)
                for (double width : {.02, .13, .33, 1.37})
                    for (unsigned border_case = 0; border_case < (mode == 1 ? 2u : 1u);
                         ++border_case)
                        for (bool flip_x : {false, true})
                            for (bool flip_y : {false, true}) {
                                SpriteRenderer sprite;
                                sprite.columns = sprite.rows = 2;
                                sprite.margin_x = sprite.margin_y = sprite.spacing_x =
                                    sprite.spacing_y = 2;
                                sprite.frame = 3;
                                sprite.image_mode = mode;
                                sprite.width = width;
                                sprite.height = width * .75;
                                sprite.pixels_per_meter = 100;
                                sprite.border_left = border_case ? 0 : 2;
                                sprite.border_right = border_case ? 0 : 3;
                                sprite.border_top = border_case ? 0 : 1;
                                sprite.border_bottom = border_case ? 0 : 2;
                                sprite.flip_x = flip_x;
                                sprite.flip_y = flip_y;
                                const auto values = sprite_draw_values(sprite, source);
                                const auto world = sprite_world_transform(sprite, {});
                                camera.orthographic_height = sprite.height;
                                const auto view = camera_view(camera, camera_world, 128, 96);
                                begin(true);
                                draw.draw(context, world, view, {}, nullptr, nullptr, nullptr, {},
                                          nullptr, {}, nullptr, {}, {}, {}, &values);
                                const auto pixels = finish();
                                for (unsigned y = 0; y < 96; ++y)
                                    for (unsigned x = 0; x < 128; ++x) {
                                        const auto px = source_pixel(
                                            mode, (x + .5) / 128, sprite.width, 100, 13,
                                            sprite.border_left, sprite.border_right, flip_x);
                                        const auto py = source_pixel(
                                            mode, (y + .5) / 96, sprite.height, 100, 9,
                                            sprite.border_top, sprite.border_bottom, flip_y);
                                        // Nearest at an exact texel boundary can round to either
                                        // side in float interpolation. Exclude only those
                                        // discontinuities.
                                        if (!linear && (std::abs(px - std::round(px)) < 1e-5 ||
                                                        std::abs(py - std::round(py)) < 1e-5)) {
                                            ++excluded;
                                            continue;
                                        }
                                        const auto sample = filtered(px, py, linear);
                                        // UNORM8 linear filtering can round an intermediate alpha
                                        // by one byte before the cutoff. Test color/opacity
                                        // numerically in Blend; exclude that classification
                                        // uncertainty only for Mask.
                                        if (alpha == MaterialAlpha::Mask &&
                                            std::abs(sample[3] - .5) <
                                                (linear ? 1. / 255 + 1e-5 : 1e-5)) {
                                            ++excluded;
                                            continue;
                                        }
                                        const auto& actual = pixels[y * 128 + x];
                                        for (unsigned lane = 0; lane < 4; ++lane) {
                                            double expected = sample[lane];
                                            if (alpha == MaterialAlpha::Mask && sample[3] < .5)
                                                expected = background[lane];
                                            else if (alpha == MaterialAlpha::Blend)
                                                expected = (lane == 3 ? sample[3]
                                                                      : sample[lane] * sample[3]) +
                                                           background[lane] * (1 - sample[3]);
                                            else if (lane == 3)
                                                expected = 1;
                                            if (std::abs(int(actual[lane]) -
                                                         int(std::lround(expected * 255))) > 2)
                                                throw std::runtime_error(
                                                    "Image fill oracle differs: mode=" +
                                                    std::to_string(mode) +
                                                    " width=" + std::to_string(width) +
                                                    " linear=" + std::to_string(linear) +
                                                    " alpha=" + std::to_string(unsigned(alpha)) +
                                                    " flip=" + std::to_string(flip_x) +
                                                    std::to_string(flip_y) + " pixel=" +
                                                    std::to_string(x) + "," + std::to_string(y) +
                                                    " lane=" + std::to_string(lane) +
                                                    " actual=" + std::to_string(actual[lane]) +
                                                    " expected=" + std::to_string(expected * 255));
                                            ++checked;
                                        }
                                    }
                                if (!linear && alpha == MaterialAlpha::Opaque && !flip_x &&
                                    !flip_y && border_case == 0 &&
                                    (width == 1.37 || (mode == 1 && width == .02))) {
                                    std::ofstream file(images / (std::to_string(mode) + "-" +
                                                                 std::to_string(width) + ".ppm"),
                                                       std::ios::binary);
                                    file << "P6\n128 96\n255\n";
                                    for (const auto& pixel : pixels)
                                        file.write(reinterpret_cast<const char*>(pixel.data()), 3);
                                }
                                ++cases;
                            }
            // One shared material/bundle must preserve independent image modes in
            // a mixed instance batch; compare with the individually proved draws.
            std::array<SpriteDrawValues, 2> values;
            std::array<AffineTransform, 2> worlds;
            for (unsigned i = 0; i < 2; ++i) {
                SpriteRenderer sprite;
                sprite.columns = sprite.rows = 2;
                sprite.margin_x = sprite.margin_y = sprite.spacing_x = sprite.spacing_y = 2;
                sprite.frame = 3;
                sprite.width = .5;
                sprite.height = 1;
                sprite.pixels_per_meter = 13;
                sprite.border_left = 2;
                sprite.border_right = 3;
                sprite.border_top = 1;
                sprite.border_bottom = 2;
                sprite.image_mode = i + 1;
                sprite.flip_y = i == 1;
                values[i] = sprite_draw_values(sprite, source);
                AffineTransform placement;
                placement.m[3] = i == 0 ? -.25 : .25;
                worlds[i] = sprite_world_transform(sprite, placement);
            }
            camera.orthographic_height = 1;
            const auto view = camera_view(camera, camera_world, 128, 96);
            begin(true);
            for (unsigned i = 0; i < 2; ++i)
                draw.draw(context, worlds[i], view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                          nullptr, {}, {}, {}, &values[i]);
            const auto individual = finish();
            const std::array<MeshDraw::Instance, 2> instances{
                {{worlds[0], {}, &values[0]}, {worlds[1], {}, &values[1]}}};
            begin(true);
            draw.draw(context, worlds[0], view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                      nullptr, instances);
            check(finish() == individual, "Mixed Sprite image modes changed when instanced");
            auto invalid = values[1];
            invalid.image.mode = 3;
            const std::array<MeshDraw::Instance, 2> broken{
                {{worlds[0], {}, &values[0]}, {worlds[1], {}, &invalid}}};
            begin(false);
            bool rejected = false;
            try {
                draw.draw(context, worlds[0], view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                          nullptr, broken);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && finish() == individual,
                  "Invalid image mapping partially drew its instance batch");
        }
    check(cases == 384 && checked > 18000000 && excluded < 10000,
          "Image fill native oracle coverage changed or skipped too many pixels");
    std::cout << cases << " native image fill cases, " << checked << " channels, " << excluded
              << " boundary pixels excluded PASS\n";
}
} // namespace forge::test_sprite_image
