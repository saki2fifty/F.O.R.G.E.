#pragma once
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <forge/engine_assets.hpp>
#include <forge/engine_texture_assets.hpp>
#include <iostream>
#include <thread>
inline void test_local_irradiance_scene(forge::DiligentPresentation& presentation,
                                        Diligent::IDeviceContext* context,
                                        const std::filesystem::path& root) {
    using namespace forge;
    using namespace Diligent;
    const auto check = test_bloom::check;
    std::filesystem::create_directories(root);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    Camera camera;
    camera.background_r = camera.background_g = camera.background_b = 0;
    scene.cameras.push_back({EntityId::generate(), camera, {}});
    RenderMesh mesh;
    mesh.entity = EntityId::generate();
    mesh.renderer.mesh = engine_primitive(0);
    mesh.renderer.materials = {{"surface", engine_material()}};
    mesh.world.m[11] = 3;
    scene.meshes.push_back(mesh);
    const auto cameras = prepare_game_cameras(scene, 32, 32);
    check(cameras.diagnostics.empty() && cameras.cameras.size() == 1,
          "Diffuse-only Scene camera was not admitted");
    FrameRenderer frame(presentation);
    frame.resources(host);
    const auto render = [&](bool warnings = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        ITextureView* image = nullptr;
        do {
            image = frame.render(context, scene, cameras.cameras, 32, 32, 0, nullptr, 0, nullptr,
                                 FrameRenderer::Output::LinearHdr);
            check(image && std::chrono::steady_clock::now() < deadline,
                  "Diffuse-only Scene resources stalled");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        if (!warnings && !frame.diagnostics().empty())
            throw std::runtime_error(frame.diagnostics().front().text);
        check(!frame.omitted_diagnostics(), "Local diffuse diagnostics were omitted");
        return test_bloom::read(presentation.device(), context, image->GetTexture());
    };
    const auto equal = [&](const auto& actual, const auto& expected, double gain = 1) {
        check(actual.size() == expected.size(), "Local diffuse Scene extents changed");
        for (unsigned i = 0; i < actual.size(); ++i)
            for (unsigned c = 0; c < 3; ++c)
                check(std::isfinite(actual[i][c]) &&
                          std::abs(actual[i][c] - expected[i][c] * gain) <= 2e-4,
                      "Local diffuse Scene resource/scale transition changed native radiance");
    };
    const auto warning = [&] {
        check(std::any_of(
                  frame.diagnostics().begin(), frame.diagnostics().end(),
                  [](const auto& d) { return d.category == "render.reflection_probe.resource"; }),
              "Local diffuse resource/capacity fallback omitted its contextual diagnostic");
    };
    const auto dark = render();
    for (const auto& pixel : dark)
        for (unsigned c = 0; c < 3; ++c)
            check(pixel[c] == 0, "Diffuse-only Scene had implicit lighting");
    ReflectionProbe source;
    source.texture = engine_texture(EngineTexture::White, TextureDimension::Cube);
    source.intensity = 0;
    source.diffuse_intensity = 1;
    source.enabled = false;
    const auto id = EntityId::generate();
    const auto select = [&] { scene.reflection_probes = {{id, source, {}}}; };
    select();
    equal(render(), dark);
    source.enabled = true;
    select();
    const auto full = render();
    check(std::count_if(full.begin(), full.end(), [](const auto& p) { return p[0] > .01; }) > 10,
          "Actual diffuse-only probe produced no Mesh illumination with a zero Scene environment");
    source.diffuse_intensity = .5;
    select();
    const auto half = render();
    equal(half, full, .5);
    source.texture = {AssetId::generate()};
    select();
    equal(render(true), half);
    warning();
    source.texture = {};
    select();
    equal(render(), dark);
    source.texture = engine_texture(EngineTexture::White, TextureDimension::Cube);
    source.diffuse_intensity = 1;
    select();
    equal(render(), full);
    source.enabled = false;
    select();
    equal(render(), dark);
    source.enabled = true;
    select();
    for (unsigned i = 1; i < 9; ++i)
        scene.reflection_probes.push_back({EntityId::generate(), source, {}});
    equal(render(true), dark);
    warning();
    scene.reflection_probes.resize(8);
    equal(render(), full);
    scene.reflection_probes.clear();
    equal(render(), dark);
    select();
    ParticleEffectData effect;
    effect.mesh = engine_primitive(0);
    effect.material = engine_material();
    effect.emission = ParticleEmission::Burst;
    effect.capacity = effect.burst_count = 1;
    effect.speed_min = effect.speed_max = 0;
    effect.acceleration = {0, 0, 0};
    effect.size_start = effect.size_end = 1;
    effect.color_start = effect.color_end = {1, 1, 1};
    const AssetRef<ParticleEffectAsset> effect_id{AssetId::generate()};
    host->preview_particle_effect(effect_id, effect);
    RenderParticleEmitter emitter;
    emitter.entity = mesh.entity;
    emitter.world = mesh.world;
    emitter.emitter.effect = effect_id;
    scene.particles = {emitter};
    scene.meshes.clear();
    equal(render(), full);
    scene.scene = AssetId::generate();
    scene.reflection_probes.clear();
    equal(render(), dark);
    context->WaitForIdle();
    std::cout << "Actual FrameRenderer diffuse-only probes, gains, leases/replacement/clear, "
                 "eight/nine capacity, Mesh particles and scene reset PASS\n";
}
