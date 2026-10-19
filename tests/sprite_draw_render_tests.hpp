#pragma once
#include "engine_render_resource.hpp"
#include "mesh_draw.hpp"
#include "texture_gpu.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace forge::test_sprite_draw {
using Pixels = std::vector<std::array<std::uint8_t, 4>>;
inline void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
inline Pixels read_pixels(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context,
                          Diligent::ITexture* texture) {
    using namespace Diligent;
    auto desc = texture->GetDesc();
    desc.Name = "FORGE Sprite test readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    check(bool(staging), "Sprite readback allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture({texture, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    check(mapped.pData != nullptr, "Sprite readback mapping failed");
    Pixels pixels(std::size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + y * desc.Width,
                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                    std::size_t(desc.Width) * 4);
    context->UnmapTextureSubresource(staging, 0, 0);
    return pixels; // The test's frame owner performs FinishFrame after readback.
}
inline std::array<std::uint8_t, 4> texel(unsigned frame, unsigned x, unsigned y) {
    constexpr std::array<std::array<std::uint8_t, 4>, 8> colors{{{{255, 0, 0, 255}},
                                                                 {{0, 255, 0, 255}},
                                                                 {{0, 0, 255, 255}},
                                                                 {{255, 255, 0, 255}},
                                                                 {{255, 0, 255, 128}},
                                                                 {{0, 255, 255, 128}},
                                                                 {{255, 128, 0, 128}},
                                                                 {{255, 255, 255, 128}}}};
    return colors[frame * 4 + (y >= 2 ? 2 : 0) + (x >= 2 ? 1 : 0)];
}
inline std::array<double, 4> filtered(unsigned frame, double u, double v, bool linear) {
    std::array<double, 4> result{};
    if (!linear) {
        const auto value =
            texel(frame, std::min(3u, unsigned(u * 4)), std::min(3u, unsigned(v * 4)));
        for (unsigned lane = 0; lane < 4; ++lane)
            result[lane] = value[lane] / 255.;
        return result;
    }
    // Independent per-frame resampling: no neighboring atlas texel participates.
    const auto x = std::clamp(u * 4 - .5, 0., 3.), y = std::clamp(v * 4 - .5, 0., 3.);
    const auto x0 = unsigned(x), y0 = unsigned(y);
    const auto x1 = std::min(3u, x0 + 1), y1 = std::min(3u, y0 + 1);
    const auto a = texel(frame, x0, y0), b = texel(frame, x1, y0);
    const auto c = texel(frame, x0, y1), d = texel(frame, x1, y1);
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
    auto* device = presentation.device();
    std::filesystem::create_directories(images);
    const auto mesh = asset_detail::engine_mesh_resource(engine_primitive(8));
    const auto gpu_mesh = upload_mesh(device, mesh.mesh);
    TextureData source;
    source.width = 8;
    source.height = 4;
    source.alpha = TextureAlpha::Straight;
    source.subresources.resize(1);
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 8; ++x)
            for (auto lane : texel(x / 4, x % 4, y))
                source.subresources[0].push_back(std::byte(lane));
    const auto atlas = upload_texture(device, source);
    MeshDraw::Textures textures;
    textures["baseColorTexture"] = atlas->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE Sprite direct profile proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth && atlas, "Sprite proof resources unavailable");
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 1;
    AffineTransform camera_world;
    camera_world.m[11] = -2;
    const auto view = camera_view(camera, camera_world, 64, 64);
    const std::array<float, 4> background{8.f / 255, 16.f / 255, 32.f / 255, 1};
    unsigned cases = 0;
    for (bool linear : {false, true})
        for (auto alpha : {MaterialAlpha::Opaque, MaterialAlpha::Mask, MaterialAlpha::Blend}) {
            MaterialData material;
            material.model = "forge.gltf.unlit.v1";
            material.double_sided = true;
            material.depth_test = material.depth_write = false;
            material.alpha = alpha;
            MaterialTextureSlot slot;
            slot.semantic = TextureSemantic::Color;
            slot.sampler.min = slot.sampler.mag =
                linear ? TextureFilter::Linear : TextureFilter::Nearest;
            material.textures["baseColorTexture"] = slot;
            MeshDraw draw(presentation, context, gpu_mesh.lods[0].parts[0], material, textures,
                          TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT, true,
                          SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                          MeshDrawProfile::Sprite);
            const auto begin = [&](bool clear) {
                presentation.begin_frame();
                auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                context->SetRenderTargets(1, &rtv,
                                          depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL),
                                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                if (clear)
                    context->ClearRenderTarget(rtv, background.data(),
                                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const Viewport viewport{0, 0, 64, 64, 0, 1};
                context->SetViewports(1, &viewport, 64, 64);
            };
            const auto end = [&] {
                presentation.end_frame(context);
                context->Flush();
                auto pixels = readback(color);
                context->FinishFrame();
                device->ReleaseStaleResources();
                return pixels;
            };
            const auto submit = [&](const SpriteDrawValues& values) {
                draw.draw(context, {}, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                          nullptr, {}, {}, {}, &values);
            };
            std::optional<MeshDraw> ordinary;
            Pixels ordinary_before;
            const auto ordinary_pixels = [&] {
                begin(true);
                ordinary->draw(context, {}, view, {});
                return end();
            };
            if (!linear && alpha == MaterialAlpha::Blend) {
                ordinary.emplace(presentation, context, gpu_mesh.lods[0].parts[0], material,
                                 textures, TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
                ordinary_before = ordinary_pixels();
                for (unsigned y = 0; y < 64; ++y)
                    for (unsigned x = 0; x < 64; ++x) {
                        const auto tx = unsigned((x + .5) / 64 * 8);
                        const auto ty = unsigned((1 - (y + .5) / 64) * 4);
                        const auto sampled = texel(tx / 4, tx % 4, ty);
                        const auto opacity = sampled[3] / 255.;
                        for (unsigned lane = 0; lane < 4; ++lane) {
                            const auto expected = lane == 3 ? 1.
                                                            : sampled[lane] / 255. * opacity +
                                                                  background[lane] * (1 - opacity);
                            check(std::abs(int(ordinary_before[y * 64 + x][lane]) -
                                           int(std::lround(expected * 255))) <= 1,
                                  "Ordinary seven-row draw differs from independent pixels");
                        }
                    }
            }
            for (unsigned frame = 0; frame < 2; ++frame)
                for (bool flip_x : {false, true})
                    for (bool flip_y : {false, true})
                        for (bool tint : {false, true}) {
                            SpriteDrawValues values{
                                sprite_region_mapping(sprite_grid_region(8, 4, {2, 1}, frame), 8, 4,
                                                      flip_x, flip_y),
                                tint ? std::array<float, 4>{.5f, .75f, .25f, .5f}
                                     : std::array<float, 4>{1, 1, 1, 1}};
                            begin(true);
                            submit(values);
                            const auto pixels = end();
                            check(pixels.size() == 4096, "Sprite target extent differs");
                            for (unsigned y = 0; y < 64; ++y)
                                for (unsigned x = 0; x < 64; ++x) {
                                    auto u = (x + .5) / 64, v = (y + .5) / 64;
                                    auto sampled = filtered(frame, flip_x ? 1 - u : u,
                                                            flip_y ? 1 - v : v, linear);
                                    for (unsigned lane = 0; lane < 4; ++lane)
                                        sampled[lane] *= values.color[lane];
                                    const auto& actual = pixels[y * 64 + x];
                                    for (unsigned lane = 0; lane < 4; ++lane) {
                                        double expected = sampled[lane];
                                        if (alpha == MaterialAlpha::Mask && sampled[3] < .5)
                                            expected = background[lane];
                                        else if (alpha == MaterialAlpha::Blend)
                                            expected = (lane == 3 ? sampled[3]
                                                                  : sampled[lane] * sampled[3]) +
                                                       background[lane] * (1 - sampled[3]);
                                        else if (lane == 3)
                                            expected = 1;
                                        check(std::abs(int(actual[lane]) -
                                                       int(std::lround(expected * 255))) <= 1,
                                              "Sprite pixels differ from independent "
                                              "frame/color/alpha oracle");
                                    }
                                }
                            if (!tint && !flip_x && !flip_y && alpha == MaterialAlpha::Blend) {
                                std::ofstream file(
                                    images / (std::string(linear ? "linear-" : "nearest-") +
                                              std::to_string(frame) + ".ppm"),
                                    std::ios::binary);
                                file << "P6\n64 64\n255\n";
                                for (const auto& pixel : pixels)
                                    file.write(reinterpret_cast<const char*>(pixel.data()), 3);
                            }
                            ++cases;
                        }
            if (alpha == MaterialAlpha::Blend) {
                SpriteDrawValues first{sprite_region_mapping({0, 0, 4, 4}, 8, 4), {1, 1, 1, .5f}};
                SpriteDrawValues second{sprite_region_mapping({4, 0, 4, 4}, 8, 4, true),
                                        {.5f, 1, .75f, .75f}};
                begin(true);
                submit(first);
                submit(second);
                const auto separate = end();
                const std::array<MeshDraw::Instance, 2> instances{
                    {{{}, {}, &first}, {{}, {}, &second}}};
                begin(true);
                draw.draw(context, {}, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                          nullptr, instances);
                check(end() == separate, "Sprite instance order or independent UV/RGBA differs");
                auto invalid = second;
                invalid.color[3] = std::numeric_limits<float>::quiet_NaN();
                begin(false);
                bool rejected = false;
                try {
                    submit(invalid);
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected && end() == separate,
                      "Invalid Sprite values issued a partial native draw");
            }
            if (ordinary)
                check(ordinary_pixels() == ordinary_before,
                      "Sprite profile leaked UV/alpha/instance state into ordinary draws");
        }
    check(cases == 96, "Sprite profile case coverage differs");
}
} // namespace forge::test_sprite_draw
