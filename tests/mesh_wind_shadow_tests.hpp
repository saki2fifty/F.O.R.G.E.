#pragma once
#include "sprite_draw_render_tests.hpp"
#include <iostream>
namespace forge::test_wind_shadow {
using namespace Diligent;
inline std::vector<float> depth(IRenderDevice* device, IDeviceContext* context, ITexture* source) {
    auto desc = source->GetDesc();
    test_sprite_draw::check(desc.Format == TEX_FORMAT_D32_FLOAT,
                            "Wind shadow requires D32F readback");
    desc.Name = "FORGE wind shadow numeric readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    test_sprite_draw::check(bool(staging), "Wind shadow readback allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture({source, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    test_sprite_draw::check(mapped.pData != nullptr, "Wind shadow readback mapping failed");
    std::vector<float> values(std::size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(values.data() + y * desc.Width,
                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                    desc.Width * sizeof(float));
    context->UnmapTextureSubresource(staging, 0, 0);
    return values;
}
inline void exercise(DiligentPresentation& presentation, IDeviceContext* context) {
    constexpr unsigned width = 192, height = 128;
    auto* device = presentation.device();
    TextureDesc desc;
    desc.Name = "FORGE wind shadow geometry comparison";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    RefCntAutoPtr<ITexture> target;
    device->CreateTexture(desc, nullptr, &target);
    test_sprite_draw::check(bool(target), "Wind shadow target allocation failed");
    auto* dsv = target->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    MaterialData material;
    material.model = "forge.gltf.unlit.v1";
    material.alpha = MaterialAlpha::Mask;
    MeshPart part;
    part.vertices = 3;
    part.indices = {0, 1, 2};
    part.streams = {{"POSITION", 3, std::vector<float>{-.7f, -.6f, 0, -.5f, .7f, 0, .8f, -.3f, 0}}};
    part.bounds = mesh_bounds(part);
    MeshData source;
    source.lods = {{1, {part}}};
    const auto gpu = upload_mesh(device, source);
    MeshDraw windy(presentation, context, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_UNKNOWN,
                   TEX_FORMAT_D32_FLOAT, false, SHADER_COMPILER_DEFAULT,
                   SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr, MeshDrawProfile::Mesh, false, true);
    MeshDraw still(presentation, context, gpu.lods[0].parts[0], material, {}, TEX_FORMAT_UNKNOWN,
                   TEX_FORMAT_D32_FLOAT, false);
    unsigned cases = 0, changed = 0;
    for (bool perspective : {false, true})
        for (bool mirrored : {false, true})
            for (double seconds : {.2, .7}) {
                Camera settings;
                settings.projection = perspective ? 0u : 1u;
                settings.orthographic_height = 4;
                settings.near_plane = .1;
                settings.far_plane = 20;
                const auto view = camera_view(settings, {}, width, height);
                AffineTransform world;
                world.m[11] = 3.5;
                if (mirrored) {
                    world.m[0] = -.8;
                    world.m[1] = .2;
                    world.m[4] = .4;
                    world.m[5] = 1.3;
                    world.m[8] = .15;
                    world.m[9] = .1;
                }
                MeshWind wind;
                wind.enabled = true;
                wind.amplitude = .7;
                wind.height = 1.4;
                wind.direction_x = 3;
                wind.direction_z = 4;
                wind.phase = 90;
                wind.frequency = .8;
                wind.wavelength = 6.3;
                const auto sample = sample_mesh_wind(wind, world, seconds);
                auto reference = source;
                auto& baked = reference.lods[0].parts[0];
                auto& positions = std::get<std::vector<float>>(baked.streams[0].values);
                const auto& input = std::get<std::vector<float>>(part.streams[0].values);
                for (unsigned vertex = 0; vertex < 3; ++vertex) {
                    auto point = world.point(
                        {input[3 * vertex], input[3 * vertex + 1], input[3 * vertex + 2]});
                    const auto h = std::clamp((point[1] - world.m[7]) / wind.height, 0., 1.);
                    point[0] += sample.displacement[0] * h * h;
                    point[2] += sample.displacement[1] * h * h;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        positions[3 * vertex + axis] = float(point[axis]);
                }
                if (mirrored)
                    std::swap(baked.indices[1], baked.indices[2]);
                baked.bounds = mesh_bounds(baked);
                const auto reference_gpu = upload_mesh(device, reference);
                MeshDraw expected(presentation, context, reference_gpu.lods[0].parts[0], material,
                                  {}, TEX_FORMAT_UNKNOWN, TEX_FORMAT_D32_FLOAT, false);
                presentation.begin_frame();
                auto draw = [&](MeshDraw& pass, const AffineTransform& matrix,
                                const MeshWindSample* value) {
                    context->SetRenderTargets(0, nullptr, dsv,
                                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    Diligent::Viewport viewport{0, 0, float(width), float(height), 0, 1};
                    context->SetViewports(1, &viewport, width, height);
                    pass.draw(context, matrix, view, {}, nullptr, nullptr, nullptr, {}, nullptr, {},
                              nullptr, {}, {}, {}, nullptr, nullptr, nullptr, {}, nullptr, {}, {},
                              value);
                    return depth(device, context, target);
                };
                const auto actual = draw(windy, world, &sample),
                           oracle = draw(expected, {}, nullptr), off = draw(still, world, nullptr);
                unsigned mismatched = 0, interior = 0;
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    const bool a = actual[i] < 1, b = oracle[i] < 1;
                    mismatched += a != b;
                    changed += a != (off[i] < 1);
                    if (a && b) {
                        ++interior;
                        test_sprite_draw::check(
                            std::abs(actual[i] - oracle[i]) < 3e-6,
                            "Wind shadow depth differs from independently baked geometry");
                    }
                }
                test_sprite_draw::check(
                    interior > 100 && mismatched <= 6,
                    "Wind shadow silhouette/winding differs from independently baked geometry");
                ++cases;
                presentation.end_frame(context);
                context->FinishFrame();
                device->ReleaseStaleResources();
            }
    test_sprite_draw::check(cases == 8 && changed > 40,
                            "Wind shadow oracle did not exercise changing silhouettes");
    std::cout
        << "Wind shadow depth/silhouette/winding: eight independent baked comparisons passed\n";
}
} // namespace forge::test_wind_shadow
