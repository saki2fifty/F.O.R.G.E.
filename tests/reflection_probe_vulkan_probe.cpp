#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "hdr_bloom_gpu_tests.hpp"
#include "surface_reflections.hpp"
#include <forge/render_view.hpp>
#include <iostream>
using namespace Diligent;
using forge::test_bloom::check;
using forge::test_bloom::Pixel;
namespace {
constexpr unsigned width = 33, height = 25;
using Colors = std::array<Pixel, 6>;
RefCntAutoPtr<ITexture> cube(IRenderDevice* device, const Colors& colors) {
    TextureDesc desc;
    desc.Name = "FORGE independent local reflection cube";
    desc.Type = RESOURCE_DIM_TEX_CUBE;
    desc.Width = desc.Height = 8;
    desc.ArraySize = 6;
    desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    std::array<std::vector<Pixel>, 6> pixels;
    std::array<TextureSubResData, 6> levels{};
    for (unsigned face = 0; face < 6; ++face) {
        pixels[face].assign(64, colors[face]);
        levels[face].pData = pixels[face].data();
        levels[face].Stride = 8 * sizeof(Pixel);
    }
    Diligent::TextureData data{levels.data(), 6};
    RefCntAutoPtr<ITexture> result;
    device->CreateTexture(desc, &data, &result);
    check(bool(result), "Probe fixture cube allocation failed");
    return result;
}
struct Fixture {
    RefCntAutoPtr<ITexture> color, indirect, surface, response, specular, depth;
    RefCntAutoPtr<ITexture> output, indirect_output, specular_output;
    forge::SurfaceReflections::LocalInput input;
    Fixture(IRenderDevice* device, unsigned projection, bool rotated = false) {
        forge::Camera settings;
        settings.projection = projection == 0 ? 1u : 0u;
        settings.orthographic_height = 4;
        settings.near_plane = .1;
        settings.far_plane = 100;
        settings.infinite_far = projection == 2;
        forge::AffineTransform camera;
        camera.m[3] = 1000000000000.25;
        camera.m[7] = -.25;
        if (rotated) {
            camera.m[0] = camera.m[5] = 0;
            camera.m[1] = -1;
            camera.m[4] = 1;
        }
        input.camera = forge::camera_view(settings, camera, width, height);
        input.projection = input.camera.projection;
        const auto image = [&](Pixel pixel) {
            return forge::test_bloom::texture(device, width, height,
                                              std::vector<Pixel>(width * height, pixel));
        };
        color = image({.75f, .5f, .875f, .25f});
        indirect = image({.25f, .25f, .375f, .5f});
        surface = image({0, 0, -1, .25f});
        std::vector<Pixel> responses(width * height, {.5f, .25f, .75f, 0});
        for (unsigned y = 0; y < height; ++y)
            responses[y * width] = {};
        response = forge::test_bloom::texture(device, width, height, responses);
        specular = image({.0625f, .0625f, .0625f, .75f});
        output = image({});
        indirect_output = image({});
        specular_output = image({});
        const auto& p = input.projection;
        std::vector<float> depths(width * height, (2 * p[10] + p[11]) / (2 * p[14] + p[15]));
        for (unsigned x = 0; x < width; ++x)
            depths[(height - 1) * width + x] = 1;
        TextureDesc desc;
        desc.Name = "FORGE independent probe plane depth";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
        TextureSubResData level;
        level.pData = depths.data();
        level.Stride = width * sizeof(float);
        Diligent::TextureData data{&level, 1};
        device->CreateTexture(desc, &data, &depth);
        check(bool(depth), "Probe fixture depth allocation failed");
        input.color = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.indirect = indirect->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.surface = surface->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.response = response->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.specular = specular->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        input.depth = depth->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }
    void draw(forge::SurfaceReflections& effect, IDeviceContext* context, bool keep_specular) {
        effect.apply_probes(
            context, input, output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            indirect_output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            keep_specular ? specular_output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr);
    }
};
void boundaries(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    Colors colors;
    colors.fill({.25f, .5f, .75f, 1});
    auto map = cube(presentation.device(), colors);
    Fixture fixture(presentation.device(), 0);
    forge::SurfaceReflections effect(presentation);
    const auto misses = presentation.cache_misses();
    for (unsigned scenario = 0; scenario < 11; ++scenario) {
        auto input = fixture.input;
        forge::SurfaceReflections::Probe probe;
        probe.volume = forge::reflection_probe_volume({}, {});
        probe.specular = map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        std::vector probes(scenario == 0 ? 0u : scenario == 1 ? 9u : 1u, probe);
        auto* output = fixture.output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        if (scenario == 2)
            probes[0].specular = fixture.input.color;
        if (scenario == 3)
            probes[0].volume.planes[0][3] = -1;
        if (scenario == 4)
            probes[0].volume.map_rotation = {0, 0};
        if (scenario == 5)
            probes[0].intensity = std::numeric_limits<double>::infinity();
        if (scenario == 6)
            probes[0].volume.center[0] = 1e100;
        if (scenario == 7)
            input.camera.right = {2, 0, 0};
        if (scenario == 8)
            input.projection[0] *= 2;
        if (scenario == 9)
            output = fixture.color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        if (scenario == 10)
            input.depth = fixture.input.color;
        input.probes = probes;
        bool rejected = false;
        try {
            effect.apply_probes(
                context, input, output,
                fixture.indirect_output->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected && presentation.cache_misses() == misses,
              "Invalid local reflection input reached native pipeline preparation");
    }
    std::cout << "11 probe admission failures rejected before pipeline mutation\n";
}
void blending(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    auto* device = presentation.device();
    std::array<RefCntAutoPtr<ITexture>, 8> maps;
    std::array<Pixel, 8> radiance{};
    for (unsigned index = 0; index < 8; ++index) {
        radiance[index] = {float(index + 1) / 16, .25f, float(8 - index) / 16, 1};
        Colors faces;
        faces.fill(radiance[index]);
        maps[index] = cube(device, faces);
    }
    forge::SurfaceReflections effect(presentation);
    double maximum_error = 0;
    unsigned cases = 0;
    for (unsigned projection = 0; projection < 3; ++projection)
        for (unsigned count : {1u, 2u, 8u})
            for (bool keep_specular : {false, true}) {
                Fixture fixture(device, projection, count == 2);
                std::vector<forge::SurfaceReflections::Probe> probes;
                for (unsigned index = 0; index < count; ++index) {
                    forge::ReflectionProbe authored;
                    authored.half_extent_x = 1.25;
                    authored.half_extent_y = 1.5;
                    authored.half_extent_z = 1.25;
                    forge::AffineTransform world;
                    world.m[3] = fixture.input.camera.position[0] + .25 * (int(index) - 4);
                    world.m[7] = fixture.input.camera.position[1];
                    world.m[11] = 2;
                    if (count == 8) {
                        world.m[0] = 2;
                        world.m[1] = .5;
                        world.m[5] = -3;
                        world.m[10] = .5;
                    }
                    probes.push_back({forge::reflection_probe_volume(authored, world),
                                      maps[index]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                      index == 0 ? .5 : 1, index == 0 ? .5 : .25, false});
                }
                fixture.input.probes = probes;
                fixture.draw(effect, context, keep_specular);
                const auto color = forge::test_bloom::read(device, context, fixture.output);
                const auto indirect =
                    forge::test_bloom::read(device, context, fixture.indirect_output);
                const auto specular =
                    keep_specular
                        ? forge::test_bloom::read(device, context, fixture.specular_output)
                        : std::vector<Pixel>{};
                for (unsigned y = 0; y < height; ++y)
                    for (unsigned x = 0; x < width; ++x) {
                        const auto at = y * width + x;
                        const auto& camera = fixture.input.camera;
                        const auto& p = camera.projection;
                        // Analytic z=2 plane; independent projection inversion.
                        const double local_x =
                            ((2. * (x + .5) / width - 1) * (2 * p[14] + p[15]) - 2 * p[2] - p[3]) /
                            p[0];
                        const double local_y =
                            ((1 - 2. * (y + .5) / height) * (2 * p[14] + p[15]) - 2 * p[6] - p[7]) /
                            p[5];
                        forge::Double3 relative{};
                        for (unsigned axis = 0; axis < 3; ++axis)
                            relative[axis] = camera.right[axis] * local_x +
                                             camera.up[axis] * local_y + camera.forward[axis] * 2;
                        double remaining = 1;
                        std::array<double, 3> local{};
                        if (x != 0 && y != height - 1)
                            for (unsigned index = 0; index < count; ++index) {
                                double margin =
                                    std::min({1.25 - std::abs(relative[0] - .25 * (int(index) - 4)),
                                              1.5 - std::abs(relative[1]),
                                              1.25 - std::abs(relative[2] - 2)});
                                if (count == 8) {
                                    const double x = relative[0] - .25 * (int(index) - 4);
                                    // World faces: x=2*localX+0.5*localY,
                                    // y=-3*localY, z=0.5*localZ. Distances to
                                    // these analytic planes stay in world metres.
                                    margin =
                                        std::min({(1.25 - std::abs(.5 * x + relative[1] / 12)) /
                                                      std::sqrt(37. / 144),
                                                  4.5 - std::abs(relative[1]),
                                                  .625 - std::abs(relative[2] - 2)});
                                }
                                const double coverage =
                                    std::clamp(margin / (index == 0 ? .5 : .25), 0., 1.);
                                for (unsigned channel = 0; channel < 3; ++channel)
                                    local[channel] += remaining * coverage *
                                                      radiance[index][channel] *
                                                      (index == 0 ? .5 : 1);
                                remaining *= 1 - coverage;
                            }
                        const double response[]{.5, .25, .75};
                        const double old_color[]{.75, .5, .875}, old_indirect[]{.25, .25, .375};
                        for (unsigned channel = 0; channel < 3; ++channel) {
                            const double replacement =
                                response[channel] * local[channel] + remaining * .0625;
                            const double change = replacement - .0625;
                            for (double error :
                                 {std::abs(color[at][channel] -
                                           std::max(0., old_color[channel] + change)),
                                  std::abs(indirect[at][channel] -
                                           std::max(0., old_indirect[channel] + change))})
                                maximum_error = std::max(maximum_error, error);
                            if (keep_specular)
                                maximum_error = std::max(
                                    maximum_error, std::abs(specular[at][channel] - replacement));
                        }
                        check(color[at][3] == .25f && indirect[at][3] == .5f &&
                                  (!keep_specular || specular[at][3] == .75f),
                              "Probe resolve changed current alpha or specular metadata");
                    }
                check(maximum_error < .002,
                      "Probe fade/order/remainder failed independent plane oracle");
                ++cases;
                context->FinishFrame();
                device->ReleaseStaleResources();
            }
    std::cout << cases << " probe blend frames PASS, max HDR error " << maximum_error << '\n';
}
void projection(forge::DiligentPresentation& presentation, IDeviceContext* context) {
    const Colors faces{{{.25f, .5f, .75f, 1},
                        {.5f, .75f, .25f, 1},
                        {.75f, .5f, .25f, 1},
                        {.25f, .75f, .5f, 1},
                        {.5f, .25f, .75f, 1},
                        {.75f, .25f, .5f, 1}}};
    auto map = cube(presentation.device(), faces);
    forge::SurfaceReflections effect(presentation);
    for (bool box : {false, true})
        for (bool yaw : {false, true}) {
            Fixture fixture(presentation.device(), 0);
            forge::ReflectionProbe authored;
            authored.half_extent_x = authored.half_extent_y = 4;
            authored.half_extent_z = 1;
            authored.rotation_degrees = yaw ? 90 : 0;
            forge::AffineTransform world;
            world.m[3] = fixture.input.camera.position[0] - 2;
            world.m[7] = fixture.input.camera.position[1];
            world.m[11] = 2;
            const std::array probes{forge::SurfaceReflections::Probe{
                forge::reflection_probe_volume(authored, world),
                map->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 1, 0, box}};
            fixture.input.probes = probes;
            fixture.draw(effect, context, true);
            const auto result =
                forge::test_bloom::read(presentation.device(), context, fixture.specular_output);
            // At image center the orthographic ray is -Z. The box exit is
            // (2,0,-1), so its dominant face is +X. A 90-degree map yaw sends
            // -Z to -X and +X to -Z, independently of the influence box.
            const auto face = box ? (yaw ? 5u : 0u) : (yaw ? 1u : 5u);
            const double response[]{.5, .25, .75};
            const auto& center = result[(height / 2) * width + width / 2];
            for (unsigned channel = 0; channel < 3; ++channel)
                check(std::abs(center[channel] - response[channel] * faces[face][channel]) < .001,
                      "Probe box projection or independent map yaw selected the wrong cube face");
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
        }
    std::cout << "4 probe box projection and map yaw frames PASS\n";
}
} // namespace
int main() {
    try {
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo settings;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(settings, &device, &context);
        check(device && context, "Probe Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        boundaries(presentation, context);
        blending(presentation, context);
        projection(presentation, context);
        context->WaitForIdle();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
