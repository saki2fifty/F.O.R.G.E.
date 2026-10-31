#pragma once
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include <chrono>
#include <forge/engine_assets.hpp>
#include <forge/engine_texture_assets.hpp>
#include <thread>

inline void test_light_image_scene(forge::DiligentPresentation& presentation,
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
    mesh.renderer.layers = 1;
    mesh.renderer.cast_shadows = false;
    mesh.world.m[11] = 3;
    scene.meshes.push_back(mesh);
    Light source;
    source.layers = 1;
    source.image_width = source.image_height = 8;
    const auto id = EntityId::generate();
    const auto select = [&] {
        RenderLight light{id, light_view(source, {}), source.texture, {}};
        if (source.texture.id)
            light.image = light_image_frame(source, {});
        scene.lights = {light};
    };
    const auto cameras = prepare_game_cameras(scene, 32, 32);
    check(cameras.diagnostics.empty() && cameras.cameras.size() == 1,
          "Light image scene camera was not admitted");
    FrameRenderer frame(presentation);
    frame.resources(host);
    const auto render = [&](bool warnings = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        ITextureView* result = nullptr;
        do {
            result = frame.render(context, scene, cameras.cameras, 32, 32, 0, nullptr, 0, nullptr,
                                  FrameRenderer::Output::LinearHdr);
            check(std::chrono::steady_clock::now() < deadline,
                  "Light image resource adoption timed out");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        if (!warnings && !frame.diagnostics().empty())
            throw std::runtime_error(frame.diagnostics().front().text);
        check(result && !frame.omitted_diagnostics(), "Light image scene output unavailable");
        return test_bloom::read(presentation.device(), context, result->GetTexture());
    };
    const auto equal = [&](const auto& actual, const auto& expected) {
        check(actual.size() == expected.size(), "Light image scene extents changed");
        for (unsigned i = 0; i < actual.size(); ++i)
            for (unsigned c = 0; c < 4; ++c)
                check(std::isfinite(actual[i][c]) && std::abs(actual[i][c] - expected[i][c]) < 2e-5,
                      "Light image resources changed a retained ordinary or complete frame");
    };
    const auto black = [&](const auto& pixels) {
        for (const auto& pixel : pixels)
            for (unsigned c = 0; c < 3; ++c)
                check(std::isfinite(pixel[c]) && pixel[c] == 0,
                      "Black light image or excluded layer retained direct illumination");
    };
    const auto failed = [&] {
        check(
            std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                        [](const auto& d) { return d.category == "render.light.image.resource"; }),
            "Failed light image omitted its actionable resource diagnostic");
    };
    select();
    const auto ordinary = render();
    check(std::any_of(ordinary.begin(), ordinary.end(), [](const auto& p) { return p[0] > .01; }),
          "Light image scene oracle had no illumination");
    source.texture = engine_texture(EngineTexture::White);
    select();
    equal(render(), ordinary);
    source.texture = engine_texture(EngineTexture::Black);
    select();
    const auto dark = render();
    black(dark);
    source.texture = {AssetId::generate()};
    select();
    equal(render(true), dark);
    failed();
    source.texture = engine_texture(EngineTexture::White, TextureDimension::Cube);
    select();
    equal(render(true), dark);
    failed();
    source.texture = {};
    select();
    equal(render(), ordinary);
    source.kind = unsigned(LightKind::Point);
    select();
    const auto point = render();
    source.texture = engine_texture(EngineTexture::White, TextureDimension::Cube);
    select();
    equal(render(), point);
    source.texture = engine_texture(EngineTexture::Black, TextureDimension::Cube);
    select();
    black(render());
    // A changed kind cannot retain an incompatible dimensional view as last-good.
    source.kind = unsigned(LightKind::Directional);
    select();
    equal(render(true), ordinary);
    failed();
    source.texture = engine_texture(EngineTexture::White);
    select();
    equal(render(), ordinary);
    source.layers = 2;
    source.texture = {AssetId::generate()};
    select();
    black(render(true));
    failed();
    source.layers = 1;
    source.texture = {};
    scene.scene = AssetId::generate();
    select();
    equal(render(), ordinary);
    // Mesh particles use the same image selection and prepared instanced draw.
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
    emitter.emitter.layers = mesh.renderer.layers;
    scene.particles = {emitter};
    scene.meshes.clear();
    const auto particles = render();
    equal(particles, ordinary);
    source.texture = engine_texture(EngineTexture::Black);
    select();
    black(render());
    source.texture = engine_texture(EngineTexture::White);
    select();
    equal(render(), particles);
    context->WaitForIdle();
}
