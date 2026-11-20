#pragma once
#include "engine_render_resource.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_draw.hpp"
#include "mesh_render_host.hpp"
#include "particle_gpu.hpp"
#include "weighted_transparency.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
namespace forge::test_weighted_particles {
inline void verify(const std::vector<test_bloom::Pixel>& pixels, unsigned width, unsigned height,
                   std::array<unsigned, 3> counts, double alpha,
                   const std::array<std::array<float, 3>, 3>& colors,
                   const std::array<float, 4>& background) {
    const auto total = counts[0] + counts[1] + counts[2];
    const double coverage = alpha == 1 ? double(total != 0) : 1 - std::pow(1 - alpha, total);
    for (unsigned y = height / 2 - 3; y < height / 2 + 3; ++y)
        for (unsigned x = width / 2 - 3; x < width / 2 + 3; ++x) {
            const double px = (double(x) + .5) / width * 2 - 1,
                         py = (double(y) + .5) / height * 2 - 1;
            std::array<double, 3> sum{};
            double denominator = 0;
            for (unsigned source = 0; source < 3; ++source) {
                const double z = source + 2, weight = counts[source] * alpha /
                                                      (1 + .01 * (px * px + py * py + z * z));
                denominator += weight;
                for (unsigned channel = 0; channel < 3; ++channel)
                    sum[channel] += colors[source][channel] * weight;
            }
            for (unsigned channel = 0; channel < 4; ++channel) {
                const double expected =
                    channel == 3 ? coverage + background[3] * (1 - coverage)
                                 : (denominator ? sum[channel] / denominator * coverage : 0) +
                                       background[channel] * (1 - coverage);
                if (!std::isfinite(pixels[y * width + x][channel]) ||
                    std::abs(pixels[y * width + x][channel] - expected) >= 3e-4)
                    throw std::runtime_error(nlohmann::json{
                        {"reason", "Device/ordinary weighted pixels differ from independent sums"},
                        {"pixel", {x, y}},
                        {"channel", channel},
                        {"counts", counts},
                        {"actual", pixels[y * width + x]},
                        {"expected", expected}}.dump());
            }
        }
}
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    constexpr unsigned width = 32, height = 32;
    TextureDesc description;
    description.Name = "FORGE device particle weighted color";
    description.Type = RESOURCE_DIM_TEX_2D;
    description.Width = width;
    description.Height = height;
    description.Format = TEX_FORMAT_RGBA32_FLOAT;
    description.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(description, nullptr, &color);
    description.Format = TEX_FORMAT_D32_FLOAT;
    description.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(description, nullptr, &depth);
    check(color && depth, "Weighted device-particle targets unavailable");
    const auto mesh =
        upload_mesh(device, asset_detail::engine_mesh_resource(engine_primitive(8)).mesh);
    Camera settings;
    settings.projection = 1;
    settings.orthographic_height = 2;
    const auto camera = camera_view(settings, {}, width, height);
    const std::array<float, 4> background{.02f, .04f, .08f, .25f};
    const std::array<std::array<float, 3>, 3> colors{
        std::array{.8f, .1f, .2f}, std::array{.2f, .8f, .1f}, std::array{.1f, .2f, .9f}};
    ParticleEffectData effect;
    effect.version = 8;
    effect.mesh = engine_primitive(8);
    effect.emission = ParticleEmission::Burst;
    effect.capacity = 128;
    effect.speed_min = effect.speed_max = 0;
    effect.acceleration = {};
    effect.size_start = effect.size_end = 1;
    effect.color_start = effect.color_end = {1, 1, 1};
    ParticleGpuInstances front(presentation, effect.capacity), back(presentation, effect.capacity);
    check(front.supported() && back.supported(),
          "Weighted particle proof requires compute support");
    WeightedTransparency pass(presentation);
    pass.prepare(color, depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL), camera.viewport);
    TextureDesc image_description;
    image_description.Name = "FORGE weighted particle alpha image";
    image_description.Type = RESOURCE_DIM_TEX_2D;
    image_description.Width = image_description.Height = 1;
    image_description.Format = TEX_FORMAT_RGBA32_FLOAT;
    image_description.Usage = USAGE_IMMUTABLE;
    image_description.BindFlags = BIND_SHADER_RESOURCE;
    const std::array<float, 4> texel{.5f, .75f, .25f, .5f};
    TextureSubResData subresource;
    subresource.pData = texel.data();
    subresource.Stride = sizeof(texel);
    Diligent::TextureData initial;
    initial.NumSubresources = 1;
    initial.pSubResources = &subresource;
    RefCntAutoPtr<ITexture> image;
    device->CreateTexture(image_description, &initial, &image);
    check(bool(image), "Weighted particle alpha image unavailable");
    unsigned frames = 0;
    for (bool textured : {false, true})
        for (unsigned births : {1u, 65u})
            for (float alpha : {0.f, .25f, 1.f}) {
                MaterialData material;
                material.model = "forge.gltf.unlit.v1";
                material.alpha = MaterialAlpha::Blend;
                material.depth_write = false;
                material.double_sided = true;
                material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                          {1, 1, 1, alpha}};
                MeshDraw::Textures textures;
                if (textured) {
                    material.textures["baseColorTexture"].semantic = TextureSemantic::Color;
                    textures["baseColorTexture"] =
                        image->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                }
                MeshDraw draw(presentation, context, mesh.lods[0].parts[0], material, textures,
                              TEX_FORMAT_RGBA32_FLOAT, TEX_FORMAT_D32_FLOAT, false,
                              SHADER_COMPILER_DEFAULT, SHADER_OPTIMIZATION_LEVEL_DEFAULT, nullptr,
                              MeshDrawProfile::MeshWeighted);
                check(draw.supports_instances(),
                      "Weighted Mesh program lost static instance support");
                effect.burst_count = births;
                AffineTransform first_world, last_world;
                first_world.m[11] = 2;
                last_world.m[11] = 4;
                const auto first_values =
                    prepare_particle_gpu_values(effect, 1, 41, first_world, camera, 1, colors[0]);
                effect.burst_count = 3;
                const auto last_values =
                    prepare_particle_gpu_values(effect, 1, 71, last_world, camera, 1, colors[2]);
                for (const auto& order :
                     {std::array{0u, 1u, 2u}, std::array{2u, 0u, 1u}, std::array{1u, 2u, 0u}}) {
                    // Real device evaluation writes each source's seven-row vertex/UAV
                    // input. This backend proof does not change authored admission.
                    front.evaluate(context, first_values, &camera);
                    back.evaluate(context, last_values, &camera);
                    check(front.count() == births && back.count() == 3,
                          "Compute lost source counts");
                    auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                    auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
                    context->SetRenderTargets(1, &rtv, dsv,
                                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    context->ClearRenderTarget(rtv, background.data(),
                                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                    pass.begin(context, dsv);
                    const auto submit = [&](const MeshDraw::InstanceBuffer* external,
                                            std::span<const MeshDraw::Instance> instances) {
                        const auto leader =
                            instances.empty() ? AffineTransform{} : instances.front().world;
                        draw.draw(context, leader, camera, {}, nullptr, nullptr, nullptr, {},
                                  nullptr, {}, nullptr, instances, {}, {}, nullptr, nullptr,
                                  nullptr, {}, nullptr, {}, {}, nullptr, {}, nullptr, external);
                    };
                    for (unsigned source : order) {
                        if (source == 1) {
                            MeshDraw::Instance ordinary;
                            ordinary.world.m[11] = 3;
                            ordinary.legacy_tint = colors[1];
                            ordinary.material_tint_only = true;
                            submit(nullptr, {&ordinary, 1});
                        } else {
                            const auto& gpu = source == 0 ? front : back;
                            for (unsigned first = 0; first < gpu.count();
                                 first += MeshDraw::instance_limit) {
                                const MeshDraw::InstanceBuffer packet{
                                    gpu.buffer(),
                                    std::uint64_t(first) * mesh_static_instance_rows *
                                        sizeof(MeshInstanceRow),
                                    std::min(gpu.count() - first, MeshDraw::instance_limit)};
                                submit(&packet, {});
                            }
                        }
                    }
                    pass.composite(context, color, dsv);
                    const auto pixels = test_bloom::read(device, context, color);
                    auto fragment_colors = colors;
                    if (textured)
                        for (auto& color : fragment_colors)
                            for (unsigned channel = 0; channel < 3; ++channel)
                                color[channel] *= texel[channel];
                    verify(pixels, width, height, {births, 1u, 3u},
                           alpha * (textured ? texel[3] : 1.f), fragment_colors, background);
                    context->FinishFrame();
                    device->ReleaseStaleResources();
                    ++frames;
                }
            }
    context->WaitForIdle();
    std::cout << "GPU_WEIGHTED_PROOF frames=" << frames
              << " real_compute/ordinary_mix/range64/orders/alpha passed\n";
}
inline void scene_exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                           const std::filesystem::path& root) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    const AssetRef<ParticleEffectAsset> effect{AssetId::generate()};
    const auto slot = asset_detail::engine_mesh_resource(engine_primitive(8)).materials.front().key;
    MaterialData surface;
    surface.model = "forge.gltf.unlit.v1";
    surface.alpha = MaterialAlpha::Blend;
    surface.depth_write = false;
    surface.double_sided = true;
    surface.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    ParticleEffectData data;
    data.mesh = engine_primitive(8);
    data.material = material;
    data.emission = ParticleEmission::Burst;
    data.capacity = 128;
    data.speed_min = data.speed_max = 0;
    data.acceleration = {};
    data.size_start = data.size_end = 1;
    const std::array<std::array<float, 3>, 3> colors{
        std::array{.8f, .1f, .2f}, std::array{.2f, .8f, .1f}, std::array{.1f, .2f, .9f}};
    const std::array<float, 4> background{.02f, .04f, .08f, .25f};
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    scene.settings.weighted_transparency = true;
    std::array<RenderParticleEmitter, 2> emitters;
    for (unsigned i = 0; i < 2; ++i) {
        emitters[i].entity = EntityId::generate();
        emitters[i].emitter.effect = effect;
        emitters[i].emitter.tint_r = colors[i * 2][0];
        emitters[i].emitter.tint_g = colors[i * 2][1];
        emitters[i].emitter.tint_b = colors[i * 2][2];
        emitters[i].world.m[11] = 2 + i * 2;
        emitters[i].time = 1;
    }
    RenderMesh ordinary;
    ordinary.entity = EntityId::generate();
    ordinary.world.m[11] = 3;
    ordinary.renderer.mesh = engine_primitive(8);
    ordinary.renderer.cast_shadows = false;
    ordinary.renderer.materials = {{slot, material}};
    // A regular Mesh shares the same transport; its color is a Material override.
    ordinary.renderer.material_parameters = {
        {slot, "baseColorFactor", {MaterialParameterType::LinearColor4, {.2f, .8f, .1f, .25f}}}};
    scene.meshes = {ordinary};
    constexpr unsigned width = 32, height = 32;
    Camera camera;
    camera.projection = 1;
    camera.orthographic_height = 2;
    auto view = camera_view(camera, {}, width, height);
    TextureDesc target;
    target.Name = "FORGE production weighted particle target";
    target.Type = RESOURCE_DIM_TEX_2D;
    target.Width = width;
    target.Height = height;
    target.Format = TEX_FORMAT_RGBA32_FLOAT;
    target.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(target, nullptr, &color);
    target.Format = TEX_FORMAT_D32_FLOAT;
    target.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(target, nullptr, &depth);
    check(color && depth, "Production weighted particle targets unavailable");
    MeshSceneRenderer renderer(host, TEX_FORMAT_RGBA32_FLOAT);
    const auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            host->pump();
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline,
                  "Weighted particle resources stalled");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
    };
    unsigned frames = 0;
    const auto render = [&](bool warning = false) {
        settle();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, background.data(),
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const auto& v = view.viewport;
        Viewport viewport{float(v.x), float(v.y), float(v.width), float(v.height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        renderer.draw(scene, view, UINT32_MAX, color, dsv);
        if (!warning && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        check(!renderer.omitted_diagnostics(), "Weighted particle diagnostics overflowed");
        auto pixels = test_bloom::read(device, context, color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        ++frames;
        return pixels;
    };
    const auto omitted = [&] {
        check(std::any_of(renderer.diagnostics().begin(), renderer.diagnostics().end(),
                          [&](const auto& diagnostic) {
                              return diagnostic.context.entity == emitters[0].entity &&
                                     diagnostic.context.property == "forge.particle_emitter" &&
                                     diagnostic.text.find("omitted from the view") !=
                                         std::string::npos;
                          }),
              "Unsupported weighted emitter lacks a scoped omission diagnostic");
        check(renderer.draw_stats().instances == 1,
              "Unsupported device alpha entered sorted draws");
    };
    for (bool billboard : {false, true}) {
        data.version = billboard ? 14 : 12;
        for (unsigned births : {1u, 65u})
            for (float alpha : {0.f, .25f, 1.f}) {
                surface.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                         {1, 1, 1, alpha}};
                host->preview_material(
                    material, {surface,
                               {{"baseColorTexture", engine_texture(EngineTexture::White)}},
                               {},
                               {}});
                scene.meshes[0].renderer.material_parameters[0].parameter.value[3] = alpha;
                data.burst_count = births;
                data.color_start = data.color_end = {1, 1, 1};
                host->preview_particle_effect(effect, data);
                for (bool reversed : {false, true}) {
                    scene.particles = reversed ? std::vector{emitters[1], emitters[0]}
                                               : std::vector{emitters[0], emitters[1]};
                    const auto pixels = render();
                    verify(pixels, width, height, {births, 1u, births}, alpha, colors, background);
                    check(renderer.draw_stats().instances == 2 * births + 1 &&
                              renderer.draw_stats().calls == (births > 64 ? 5u : 3u),
                          "Production weighted device instance slices/counts differ");
                }
            }
    }
    // Disabled admission must leave only the ordinary Mesh; no retained device image.
    scene.settings.weighted_transparency = false;
    const auto disabled = render(true);
    omitted();
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = material;
    sprite.renderer.tint_a = .4f;
    sprite.world.m[3] = .75;
    sprite.world.m[11] = 1;
    scene.sprites = {sprite};
    scene.settings.weighted_transparency = true;
    (void)render(true);
    check(std::any_of(renderer.diagnostics().begin(), renderer.diagnostics().end(),
                      [](const auto& diagnostic) {
                          return diagnostic.category == "render.transparency.unavailable";
                      }),
          "Incompatible Sprite did not refuse complete-view weighted admission");
    check(renderer.draw_stats().instances == 2, "Mixed view sent device particles to sorted alpha");
    scene.sprites.clear();
    const auto recovered = render();
    scene.settings.weighted_transparency = false;
    check(render(true) == disabled, "Refused device view retained a previous weighted image");
    scene.settings.weighted_transparency = true;
    check(render() == recovered, "Weighted view did not recover after an incompatible source");
    // A second camera uses its own device rows; returning cannot reuse shifted rows.
    AffineTransform moved;
    moved.m[3] = .125;
    view = camera_view(camera, moved, width, height);
    (void)render();
    view = camera_view(camera, {}, width, height);
    check(render() == recovered, "Another camera corrupted retained weighted device state");
    context->WaitForIdle();
    std::cout << "GPU_WEIGHTED_SCENE frames=" << frames
              << " explicit_profiles/ordinary_mix/fail_closed/recovery/views passed\n";
}
} // namespace forge::test_weighted_particles
