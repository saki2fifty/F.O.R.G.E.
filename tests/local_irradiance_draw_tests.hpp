#pragma once
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include "reflection_probe_volume.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
inline void test_local_irradiance_draws(forge::DiligentPresentation& presentation,
                                        Diligent::IDeviceContext* context,
                                        const forge::GpuMeshPart& mesh, Diligent::ITexture* target,
                                        Diligent::ITextureView* depth,
                                        const forge::AffineTransform& source_world,
                                        const forge::CameraView& camera) {
    using namespace forge;
    using namespace Diligent;
    const auto check = test_bloom::check;
    auto* device = presentation.device();
    auto& pbr = presentation.pbr(context);
    const float unit[]{1, 1, 1, 1}, tinted[]{.25f, .5f, .75f, 1};
    auto white = pbr.CreateIrradianceCube(context, "FORGE local diffuse white reference",
                                          TEX_FORMAT_RGBA32_FLOAT, 4, unit);
    auto color = pbr.CreateIrradianceCube(context, "FORGE local diffuse color source",
                                          TEX_FORMAT_RGBA32_FLOAT, 4, tinted);
    auto black = presentation.black_environment(context);
    GpuEnvironment diffuse_maps, specular_maps, full_maps;
    diffuse_maps.source = diffuse_maps.diffuse = white;
    diffuse_maps.specular = diffuse_maps.sheen = black->GetTexture();
    specular_maps.source = specular_maps.specular = specular_maps.sheen = white;
    specular_maps.diffuse = black->GetTexture();
    full_maps.source = full_maps.diffuse = full_maps.specular = full_maps.sheen = white;
    EnvironmentLighting diffuse{&diffuse_maps, 1, 0}, specular{&specular_maps, .4f, 0},
        full{&full_maps, .4f, 0};
    const auto opaque =
        test_bloom::texture(device, 32, 32, std::vector<test_bloom::Pixel>(1024, {0, 0, 0, 1}));
    TransmissionBackground snapshot(presentation);
    const auto background = snapshot.capture(context, opaque, camera.viewport);
    ReflectionProbe source;
    source.half_extent_x = source.half_extent_y = source.half_extent_z = 100;
    AffineTransform placement;
    for (unsigned axis = 0; axis < 3; ++axis)
        placement.m[4 * axis + 3] = camera.position[axis];
    EnvironmentProbe probe;
    probe.volume = reflection_probe_volume(source, placement);
    probe.intensity = 0;
    probe.diffuse_intensity = .75;
    probe.blend_distance = 0;
    probe.irradiance = color->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    Light direct;
    const auto illumination = light_view(direct, {});
    unsigned cases = 0;
    for (unsigned variant = 0; variant < 4; ++variant) {
        MaterialData material;
        material.model = "forge.gltf.metallic-roughness.v1";
        material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                  {.6f, .3f, .2f, 1}};
        material.parameters["metallicFactor"] = {MaterialParameterType::Scalar,
                                                 {variant == 1 ? 1.f : 0.f}};
        material.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.6f}};
        material.parameters["emissiveFactor"] = {MaterialParameterType::LinearColor3,
                                                 {.07f, .11f, .03f}};
        if (variant == 2) {
            material.parameters["clearcoatFactor"] = {MaterialParameterType::Scalar, {.75f}};
            material.parameters["sheenColorFactor"] = {MaterialParameterType::LinearColor3,
                                                       {.2f, .1f, .3f}};
        }
        if (variant == 3)
            material.parameters["transmissionFactor"] = {MaterialParameterType::Scalar, {.75f}};
        MeshDraw draw(presentation, context, mesh, material, {}, TEX_FORMAT_RGBA32_FLOAT,
                      TEX_FORMAT_D32_FLOAT);
        check(draw.supports_local_irradiance(), "Current lit draw lost local diffuse support");
        auto world = source_world;
        const auto render = [&](const EnvironmentLighting* environment,
                                std::span<const EnvironmentProbe> probes = {}) {
            auto* output = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            context->SetRenderTargets(1, &output, depth, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[4]{};
            context->ClearRenderTarget(output, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearDepthStencil(depth, CLEAR_DEPTH_FLAG, 1, 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const Viewport viewport{0, 0, 32, 32, 0, 1};
            context->SetViewports(1, &viewport, 32, 32);
            draw.bind_environment(environment ? environment->maps : nullptr);
            draw.draw(context, world, camera, std::span(&illumination, 1), environment, nullptr,
                      nullptr, {}, variant == 3 ? &background : nullptr, {}, nullptr, {}, {}, {},
                      nullptr, nullptr, nullptr, {}, nullptr, {}, probes);
            return test_bloom::read(device, context, target);
        };
        const auto equal = [&](const auto& a, const auto& b, const char* message) {
            for (unsigned i = 0; i < a.size(); ++i)
                for (unsigned c = 0; c < 4; ++c)
                    check(std::isfinite(a[i][c]) && std::abs(a[i][c] - b[i][c]) <=
                                                        3e-5f * std::max(1.f, std::abs(b[i][c])),
                          message);
        };
        for (bool mirrored : {false, true}) {
            world = source_world;
            if (mirrored)
                world.m[0] *= -1;
            const auto base = render(nullptr), white_reference = render(&diffuse);
            const auto ordinary_specular = render(&specular), ordinary_full = render(&full);
            for (bool global : {false, true}) {
                const auto actual = render(global ? &full : nullptr, std::span(&probe, 1));
                const auto& preserved = global ? ordinary_specular : base;
                unsigned covered = 0;
                for (unsigned i = 0; i < actual.size(); ++i)
                    for (unsigned c = 0; c < 3; ++c) {
                        const double expected =
                            preserved[i][c] +
                            (white_reference[i][c] - base[i][c]) * (.75 * tinted[c]);
                        check(std::isfinite(actual[i][c]) &&
                                  std::abs(actual[i][c] - expected) <=
                                      3e-5 * std::max(1., std::abs(expected)),
                              "Actual Mesh diffuse probes changed native diffuse gain, "
                              "direct/emission or other IBL lobes");
                        covered += actual[i][c] > .001;
                    }
                check(covered > 100, "Local diffuse product oracle had no visible receiver");
                auto outside = probe;
                outside.volume.center[0] += 1000;
                equal(render(global ? &full : nullptr, std::span(&outside, 1)),
                      global ? ordinary_full : base,
                      "Outside local volume changed ordinary native lighting");
                equal(render(global ? &full : nullptr), global ? ordinary_full : base,
                      "Removing local diffuse probes left stale prepared draw bindings");
                ++cases;
            }
        }
        for (unsigned invalid = 0; invalid < 4; ++invalid) {
            auto bad = probe;
            if (invalid == 0)
                bad.irradiance = pbr.GetWhiteTexSRV();
            if (invalid == 1)
                bad.diffuse_intensity = std::numeric_limits<double>::quiet_NaN();
            if (invalid == 2)
                bad.volume.planes[0][3] = 0;
            const std::vector<EnvironmentProbe> probes(invalid == 3 ? 9 : 1, bad);
            bool rejected = false;
            try {
                (void)render(nullptr, probes);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected,
                  "Invalid local diffuse maps/values/complete-set capacity were admitted");
        }
        draw.clear_local_irradiance();
    }
    context->WaitForIdle();
    std::cout << cases
              << " actual Mesh local diffuse cases PASS: native PBR layers, zero/global "
                 "environment, large origins, winding/clear/outside and complete input rejection\n";
}
