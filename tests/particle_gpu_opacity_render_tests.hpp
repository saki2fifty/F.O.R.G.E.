#pragma once
#include "engine_render_resource.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include "particle_gpu.hpp"
#include "weighted_transparency.hpp"
namespace forge::test_gpu_opacity {
// The established CPU owner evaluates the specified device-precision appearance.
// Birth membership remains separate: a live GPU birth can round its visual age to1.
inline ParticleSample appearance(ParticleEffectData source, const ParticleGpuValues& values,
                                 const ParticleGpuOpacityValues& opacity, unsigned id) {
    const float age = std::max(0.f, values.timing[0] - float(id) * values.timing[1]);
    const float u = std::clamp(age / values.timing[2], 0.f, 1.f);
    source.version = 6;
    source.emission = ParticleEmission::Burst;
    source.capacity = source.burst_count = 1;
    source.lifetime = 1;
    source.opacity_start = opacity.endpoints[0];
    source.opacity_end = opacity.endpoints[1];
    source.opacity_curve.clear();
    for (unsigned i = 0; i < opacity.count; ++i)
        source.opacity_curve.push_back(
            {opacity.stops[i][0], opacity.stops[i][1], opacity.stops[i][2] != 0});
    if (u < 1)
        return sample_particle_effect(source, double(u), 0, 1).front();
    ParticleSample result;
    result.opacity =
        source.opacity_curve.empty() ? source.opacity_end : source.opacity_curve.back().value;
    result.color = source.color_end;
    return result;
}
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    constexpr unsigned width = 32, height = 32;
    TextureDesc target;
    target.Name = "FORGE GPU lifetime alpha numeric target";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = width;
    target.Height = height;
    target.Format = TEX_FORMAT_RGBA32_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth, image;
    device->CreateTexture(target, nullptr, &color);
    target.Format = TEX_FORMAT_D32_FLOAT;
    target.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(target, nullptr, &depth);
    target.Name = "FORGE GPU lifetime alpha image";
    target.Width = target.Height = 1;
    target.Format = TEX_FORMAT_RGBA32_FLOAT;
    target.BindFlags = BIND_SHADER_RESOURCE;
    target.Usage = USAGE_IMMUTABLE;
    const std::array<float, 4> texel{.5f, .75f, .25f, .5f};
    TextureSubResData subresource;
    subresource.pData = texel.data();
    subresource.Stride = sizeof(texel);
    Diligent::TextureData initial{&subresource, 1};
    device->CreateTexture(target, &initial, &image);
    check(color && depth && image, "GPU lifetime alpha targets/image unavailable");
    Camera settings;
    settings.projection = 1;
    settings.orthographic_height = 2;
    const auto camera = camera_view(settings, {}, width, height);
    auto mesh = upload_mesh(device, asset_detail::engine_mesh_resource(engine_primitive(8)).mesh);
    ParticleEffectData effect;
    effect.mesh = engine_primitive(8);
    effect.capacity = 128;
    effect.burst_count = 65;
    effect.spawn_rate = 128;
    effect.lifetime = 1;
    effect.speed_min = effect.speed_max = 0;
    effect.acceleration = {};
    effect.size_start = effect.size_end = 1;
    effect.color_start = {.8f, .3f, .1f};
    effect.color_end = {.1f, .6f, .9f};
    ParticleGpuInstances gpu(presentation, effect.capacity);
    WeightedTransparency pass(presentation);
    pass.prepare(color, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), camera.viewport);
    const std::array<float, 4> background{.02f, .04f, .08f, .25f};
    AffineTransform world;
    world.m[11] = 3;
    unsigned frames = 0;
    for (bool textured : {false, true}) {
        MaterialData material;
        material.model = "forge.gltf.unlit.v1";
        material.alpha = MaterialAlpha::Blend;
        material.depth_write = false;
        material.double_sided = true;
        material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                  {1, 1, 1, .6f}};
        MeshDraw::Textures textures;
        if (textured) {
            material.textures["baseColorTexture"].semantic = TextureSemantic::Color;
            textures["baseColorTexture"] = image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }
        MeshDraw draw(presentation, context, mesh.lods[0].parts[0], material, textures,
                      TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT, false, SHADER_COMPILER_DEFAULT,
                      SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr, MeshDrawProfile::MeshWeighted);
        for (bool billboard : {false, true})
            for (auto emission : {ParticleEmission::Burst, ParticleEmission::Continuous})
                for (unsigned profile : {0u, 1u, 2u, 3u, 4u})
                    for (double time : {0., .125, .75, std::nextafter(1., 0.)}) {
                        effect.version = billboard ? 14 : 12; // Backend proof, no source admission.
                        effect.emission = emission;
                        ParticleGpuOpacityValues opacity;
                        if (profile < 3)
                            opacity.endpoints.fill(profile == 0 ? 0.f : profile == 1 ? .25f : 1.f);
                        else {
                            opacity.count = 3;
                            opacity.stops[0] = {0, 0, 1, 0};
                            opacity.stops[1] = {.5f, 1, profile == 3 ? 1.f : 0.f, 0};
                            opacity.stops[2] = {1, .25f, 0, 0};
                        }
                        const auto values =
                            prepare_particle_gpu_values(effect, time, 41, world, camera);
                        gpu.evaluate(context, values, billboard ? &camera : nullptr, true,
                                     &opacity);
                        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
                        context->SetRenderTargets(1, &rtv, dsv,
                                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        context->ClearRenderTarget(rtv, background.data(),
                                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        pass.begin(context, dsv);
                        for (unsigned first = 0; first < gpu.count();
                             first += MeshDraw::instance_limit) {
                            const MeshDraw::InstanceBuffer packet{
                                gpu.buffer(),
                                std::uint64_t(first) * mesh_static_instance_rows *
                                    sizeof(MeshInstanceRow),
                                std::min(gpu.count() - first, MeshDraw::instance_limit)};
                            draw.draw(context, {}, camera, {}, nullptr, nullptr, nullptr, {},
                                      nullptr, {}, nullptr, {}, {}, {}, nullptr, nullptr, nullptr,
                                      {}, nullptr, {}, {}, nullptr, {}, nullptr, &packet);
                        }
                        pass.composite(context, color, dsv);
                        const auto pixels = test_bloom::read(device, context, color);
                        double denominator = 0, transmittance = 1;
                        std::array<double, 3> sum{};
                        for (unsigned id = 0; id < gpu.count(); ++id) {
                            const auto sample = appearance(effect, values, opacity, id);
                            const double alpha = .6f * (textured ? texel[3] : 1.f) * sample.opacity;
                            denominator += alpha;
                            transmittance *= 1 - alpha;
                            for (unsigned channel = 0; channel < 3; ++channel)
                                sum[channel] += sample.color[channel] *
                                                (textured ? texel[channel] : 1.f) * alpha;
                        }
                        for (unsigned y = 13; y < 19; ++y)
                            for (unsigned x = 13; x < 19; ++x)
                                for (unsigned channel = 0; channel < 4; ++channel) {
                                    const double expected =
                                        channel == 3
                                            ? 1 - transmittance + background[3] * transmittance
                                            : (denominator ? sum[channel] / denominator *
                                                                 (1 - transmittance)
                                                           : 0) +
                                                  background[channel] * transmittance;
                                    if (!std::isfinite(pixels[y * width + x][channel]) ||
                                        std::abs(pixels[y * width + x][channel] - expected) > 3e-4)
                                        throw std::runtime_error(nlohmann::json{
                                            {"reason", "GPU lifetime pixels differ from "
                                                       "independent appearance/composition"},
                                            {"frame", frames},
                                            {"profile", profile},
                                            {"time", time},
                                            {"channel", channel},
                                            {"actual", pixels[y * width + x]},
                                            {"expected", expected}}.dump());
                                }
                        context->FinishFrame();
                        device->ReleaseStaleResources();
                        ++frames;
                    }
    }
    context->WaitForIdle();
    std::cout << "GPU_OPACITY_PIXELS frames=" << frames
              << " device/weighted/texture/Material/range64/Step/continuous/burst passed\n";
}
} // namespace forge::test_gpu_opacity
