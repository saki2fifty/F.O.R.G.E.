#pragma once
#include "engine_render_resource.hpp"
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_render_host.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include "sprite_draw_render_tests.hpp"
#include "view_image.hpp"
#include <chrono>
#include <forge/game_content.hpp>
#include <forge/runtime_world.hpp>
#include <thread>
namespace forge::test_particle_alpha {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& root) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    std::filesystem::create_directories(root);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialData values;
    values.model = "forge.gltf.unlit.v1";
    values.alpha = MaterialAlpha::Blend;
    values.depth_write = false;
    values.double_sided = true;
    values.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4, {1, 1, 1, .5f}};
    host->preview_material(material, {values, {}, {}});
    const AssetRef<ParticleEffectAsset> effect{AssetId::generate()};
    ParticleEffectData data;
    data.version = 5;
    data.mesh = engine_primitive(8);
    data.material = material;
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 1;
    data.speed_min = data.speed_max = 0;
    data.acceleration = {0, 0, 0};
    data.size_start = data.size_end = 1;
    data.color_start = data.color_end = {1, 1, 1};
    host->preview_particle_effect(effect, data);
    MeshSceneRenderer renderer(host, TEX_FORMAT_RGBA32_FLOAT);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    const std::array<float, 3> front{.8f, .1f, .2f}, back{.1f, .2f, .9f};
    for (unsigned i = 0; i < 2; ++i) {
        RenderParticleEmitter emitter;
        emitter.entity = EntityId::generate();
        emitter.emitter.effect = effect;
        emitter.time = 1;
        emitter.world.m[11] = i ? 4 : 2;
        const auto tint = i ? back : front;
        emitter.emitter.tint_r = tint[0];
        emitter.emitter.tint_g = tint[1];
        emitter.emitter.tint_b = tint[2];
        scene.particles.push_back(emitter);
    }
    constexpr unsigned width = 32, height = 32;
    TextureDesc target;
    target.Name = "FORGE sorted-alpha particle numeric target";
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
    check(color && depth, "Sorted-alpha particle targets unavailable");
    Camera camera;
    camera.projection = 1;
    camera.orthographic_height = 2;
    AffineTransform camera_world;
    unsigned frames = 0;
    auto draw = [&](bool expected_diagnostic = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            host->pump();
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline,
                  "Sorted-alpha particle preparation timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]{};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, float(width), float(height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        renderer.draw(scene, camera_view(camera, camera_world, width, height), UINT32_MAX, color,
                      dsv);
        if (!expected_diagnostic && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        check(!expected_diagnostic || !renderer.diagnostics().empty(),
              "Sorted-alpha diagnostic disappeared");
        auto pixels = test_bloom::read(device, context, color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        ++frames;
        return pixels;
    };
    auto assert_center = [&](const auto& pixels, const std::array<float, 3>& expected) {
        for (unsigned y = 14; y < 18; ++y)
            for (unsigned x = 14; x < 18; ++x)
                for (unsigned c = 0; c < 3; ++c)
                    if (!std::isfinite(pixels[y * width + x][c]) ||
                        std::abs(pixels[y * width + x][c] - expected[c]) > 2e-4f)
                        throw std::runtime_error(nlohmann::json{
                            {"reason", "Sorted-alpha particle composition differs"},
                            {"frame", frames},
                            {"pixel", {x, y}},
                            {"actual", pixels[y * width + x]},
                            {"expected", expected}}.dump());
    };
    std::array<float, 3> expected;
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = front[c] * .5f + back[c] * .25f;
    const auto sorted = draw();
    assert_center(sorted, expected);
    check(renderer.draw_stats().instances == 2 && renderer.draw_stats().calls == 2 &&
              !renderer.draw_stats().batched_calls,
          "Transparent particles were merged into an unordered instance batch");
    std::reverse(scene.particles.begin(), scene.particles.end());
    check(draw() == sorted, "Emitter insertion order changed sorted-alpha pixels");
    scene.settings.weighted_transparency = true;
    check(draw(true) == sorted, "Weighted fallback changed transparent particle pixels");
    check(std::all_of(
              renderer.diagnostics().begin(), renderer.diagnostics().end(),
              [](auto& d) { return d.text.find("sorted alpha retained") != std::string::npos; }),
          "Weighted fallback hid another particle error");
    scene.settings.weighted_transparency = false;
    camera_world.m = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 6};
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = back[c] * .5f + front[c] * .25f;
    assert_center(draw(), expected);
    camera_world = {};
    check(draw() == sorted, "Second camera poisoned original particle ordering");
    RenderMesh middle;
    middle.entity = EntityId::generate();
    middle.renderer.mesh = data.mesh;
    middle.renderer.cast_shadows = false;
    const auto slot = asset_detail::engine_mesh_resource(data.mesh).materials.front().key;
    middle.renderer.materials = {{slot, material}};
    const std::array<float, 3> green{.2f, .8f, .1f};
    middle.renderer.material_parameters = {
        {slot,
         "baseColorFactor",
         {MaterialParameterType::LinearColor4, {green[0], green[1], green[2], .25f}}}};
    middle.world.m[11] = 3;
    scene.meshes.push_back(middle);
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = front[c] * .5f + green[c] * .125f + back[c] * .1875f;
    assert_center(draw(), expected);
    scene.meshes[0].renderer.materials = {{slot, engine_material(EngineMaterial::TwoSided)}};
    scene.meshes[0].renderer.material_parameters.clear();
    const auto emitters = std::exchange(scene.particles, {});
    const auto opaque = draw();
    scene.particles = emitters;
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = front[c] * .5f + opaque[height / 2 * width + width / 2][c] * .5f;
    assert_center(draw(), expected);
    scene.meshes.clear();
    for (unsigned version : {1u, 2u, 3u}) {
        data.version = version;
        host->preview_particle_effect(effect, data);
        const auto rejected = draw(true);
        check(renderer.draw_stats().instances == 0 &&
                  std::all_of(rejected.begin(), rejected.end(),
                              [](const auto& p) { return p[0] == 0 && p[1] == 0 && p[2] == 0; }),
              "Opaque/GPU profile admitted a Blend particle material");
    }
    data.version = 5;
    host->preview_particle_effect(effect, data);
    check(draw() == sorted, "Restoring CPU alpha did not restore sorted pixels");
    data.version = 6;
    host->preview_particle_effect(effect, data);
    check(draw() == sorted, "Full particle opacity changed the prior alpha profile's pixels");
    data.opacity_start = data.opacity_end = 0;
    host->preview_particle_effect(effect, data);
    assert_center(draw(), {0, 0, 0});
    data.opacity_start = 1;
    host->preview_particle_effect(effect, data);
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = front[c] * .25f + back[c] * .1875f;
    const auto fading = draw();
    assert_center(fading, expected);
    check(fading != sorted, "Opacity over life did not reach the actual Material alpha path");
    data.opacity_curve = {{0, 0, false}, {1, 1, false}};
    host->preview_particle_effect(effect, data);
    assert_center(draw(), expected);
    data.opacity_curve = {{0, .2, true}, {1, 1, false}};
    host->preview_particle_effect(effect, data);
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = front[c] * .1f + back[c] * .09f;
    assert_center(draw(), expected);
    data.opacity_curve.clear();
    data.emission = ParticleEmission::Continuous;
    data.capacity = 2;
    data.spawn_rate = 1;
    for (auto& emitter : scene.particles)
        emitter.time = 2.5;
    host->preview_particle_effect(effect, data);
    // Birth ages1.5/.5 give opacity.25/.75, times Material alpha.5.
    // Same-color layers combine to A=.125+.375*(1-.125)=.453125.
    constexpr float combined = .453125f;
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = front[c] * combined + back[c] * combined * (1 - combined);
    const auto multiple = draw();
    assert_center(multiple, expected);
    check(renderer.draw_stats().instances == 4 && renderer.draw_stats().calls == 4,
          "Per-birth opacity merged transparent particles or lost their membership");
    scene.settings.weighted_transparency = true;
    check(draw(true) == multiple, "Weighted fallback lost per-birth opacity");
    scene.settings.weighted_transparency = false;
    camera_world.m = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 6};
    for (unsigned c = 0; c < 3; ++c)
        expected[c] = back[c] * combined + front[c] * combined * (1 - combined);
    assert_center(draw(), expected);
    camera_world = {};
    middle.renderer.materials = {{slot, engine_material(EngineMaterial::TwoSided)}};
    middle.renderer.material_parameters.clear();
    scene.meshes.push_back(middle);
    for (unsigned c = 0; c < 3; ++c)
        expected[c] =
            front[c] * combined + opaque[height / 2 * width + width / 2][c] * (1 - combined);
    assert_center(draw(), expected);
    scene.meshes.clear();
    data.version = 5;
    data.opacity_start = data.opacity_end = 1;
    data.emission = ParticleEmission::Burst;
    data.capacity = 1;
    for (auto& emitter : scene.particles)
        emitter.time = 1;
    host->preview_particle_effect(effect, data);
    check(draw() == sorted, "Opacity profile recovery changed earlier sorted-alpha pixels");
    data.version = 11;
    data.lifetime = 4;
    data.acceleration = {0, -2, 0};
    data.collision = ParticleCollisionPlane{{0, 1, 0}, -1, 0, 1, 0, 1};
    host->preview_particle_effect(effect, data);
    for (double age : {.5, 1., 1.5, 2., 2.75, 3.}) {
        const auto emitters = std::exchange(scene.particles, {});
        if (age < 3)
            for (const auto& emitter : emitters) {
                RenderMesh reference;
                reference.entity = emitter.entity;
                reference.renderer.mesh = data.mesh;
                reference.renderer.cast_shadows = false;
                reference.renderer.materials = {{slot, material}};
                reference.renderer.material_parameters = {
                    {slot,
                     "baseColorFactor",
                     {MaterialParameterType::LinearColor4,
                      {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b,
                       .5f}}}};
                reference.world = emitter.world;
                const auto phase = age - 1;
                // Independent exact trajectory: fall to y=-1 at t=1, then one
                // elastic gravity arc. The next incoming contact at t=3 kills.
                reference.world.m[7] += age < 1 ? -age * age : -1 + 2 * phase - phase * phase;
                scene.meshes.push_back(std::move(reference));
            }
        const auto expected_collision = draw();
        scene.meshes.clear();
        scene.particles = emitters;
        for (auto& emitter : scene.particles)
            emitter.time = age;
        check(draw() == expected_collision &&
                  renderer.draw_stats().instances == (age < 3 ? 2u : 0u),
              "Plane collision rendering differs from independently placed ordinary Meshes");
    }
    data.version = 5;
    data.lifetime = 2;
    data.acceleration = {};
    data.collision.reset();
    for (auto& emitter : scene.particles)
        emitter.time = 1;
    host->preview_particle_effect(effect, data);
    check(draw() == sorted, "Plane collision left stale geometry after restoring an older profile");
    context->WaitForIdle();
    std::ofstream receipt(root / "particle-alpha.json");
    receipt << nlohmann::json{{"frames", frames},
                              {"target", {width, height}},
                              {"scope", "Native isolated renderer; no publication/export or "
                                        "physical performance claim"}}
                   .dump(2);
    check(bool(receipt.flush()), "Sorted-alpha particle evidence write failed");
    std::cout << "PARTICLE_ALPHA frames=" << frames
              << " analytical blend/sort/cameras/opaque/weighted/profile recovery passed\n";
}
inline void gallery(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& content, const std::filesystem::path& images,
                    bool opacity = false, bool billboards = false, bool gpu = false,
                    bool graph = false, bool trails = false, bool collision = false,
                    bool weighted = false, bool gpu_opacity = false, bool sorted = false,
                    bool scene_collision = false, bool scene_collision_2d = false) {
    using test_bloom::check;
#ifdef _WIN32
    const RuntimePackageTarget target{"windows", "d3d12"};
#else
    const RuntimePackageTarget target{"linux", "none"};
#endif
    const auto catalog =
        std::make_shared<const AssetCatalog>(open_runtime_content(content, target));
    AssetId scene, effect;
    for (const auto& [id, record] : catalog->records()) {
        if (record.type == SceneAsset::type)
            scene = id;
        if (record.type == ParticleEffectAsset::type)
            effect = id;
    }
    check(catalog->records().size() == (graph        ? 5u
                                        : billboards ? 4u
                                                     : 3u) &&
              scene && effect,
          "Sorted-alpha content closure differs from Scene, Effect and Material");
    ResourcePool<ParticleEffectAsset> effects;
    const auto ticket = request_particle_effect(effects, content, catalog, {effect});
    check(effects.wait(ticket, std::chrono::seconds(15)), "Source-free alpha Effect load stalled");
    const auto selected = effects.acquire(ticket);
    check(selected &&
              selected->version == (scene_collision_2d ? 53u
                                    : scene_collision  ? 39u
                                    : sorted           ? 34u
                                    : gpu && trails    ? (gpu_opacity ? 25u
                                                          : weighted  ? 24u
                                                                      : 23u)
                                    : gpu && collision ? (gpu_opacity ? 22u
                                                          : weighted  ? 21u
                                                                      : 20u)
                                    : gpu_opacity      ? (graph ? 19u : 18u)
                                    : weighted         ? (graph ? 15u : 14u)
                                    : collision        ? 11u
                                    : trails           ? 10u
                                    : graph            ? 9u
                                    : gpu              ? 8u
                                    : billboards       ? 7u
                                    : opacity          ? 6u
                                                       : 5u) &&
              bool(selected->program) == graph,
          "Source-free alpha content selected another evaluation profile");
    RuntimeWorld world({}, {}, std::nullopt, content, false);
    world.scene.restore_snapshot(load_game_scene(content, {scene}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (!world.prepare_physics_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < deadline,
              "Alpha RuntimeWorld preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto particles = particle_runtime(world.engine.world());
    while (!particles->prepare()) {
        particles->synchronize();
        check(std::chrono::steady_clock::now() < deadline, "Alpha fixed-clock preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (unsigned i = 0; i < 12; ++i)
        world.simulation.tick(.25f);
    auto snapshot = extract_render_scene(world.simulation.presentation(1));
    check(snapshot.diagnostics.empty() && snapshot.particles.size() == 2 &&
              snapshot.cameras.size() == 1 && snapshot.meshes.size() == (scene_collision ? 2u : 1u),
          "Source-free alpha Game lost its native consumers");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, content);
    host->catalog(catalog);
    FrameRenderer renderer(presentation);
    renderer.resources(host);
    auto draw = [&](bool play_notice = false) {
        test_sprite_draw::Pixels pixels;
        bool notice_seen = false;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        do {
            presentation.begin_frame();
            auto* view = renderer.game(context, snapshot, 640, 360);
            check(view, "Source-free alpha Game output missing");
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, view->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            for (const auto& diagnostic : renderer.diagnostics()) {
                if (play_notice && diagnostic.category == "render.particles.play_required" &&
                    diagnostic.severity == Severity::Info)
                    notice_seen = true;
                else
                    throw std::runtime_error(diagnostic.text);
            }
            check(std::chrono::steady_clock::now() < end, "Alpha Game resource adoption stalled");
        } while (renderer.pending());
        check(!play_notice || notice_seen, "Stopped Scene response omitted its Play notice");
        return pixels;
    };
    const auto captured = snapshot;
    const auto rendered = draw();
    if (scene_collision) {
        for (auto& emitter : snapshot.particles) {
            check(emitter.world_samples && !emitter.world_samples->empty(),
                  "Scene Game has no copied native collision observations");
            emitter.world.m[3] += 1000;
        }
        check(draw() == rendered,
              "World-space particle pixels/culling followed the current emitter proxy");
    }
    snapshot.particles.clear();
    const auto empty = draw();
    unsigned changed = 0;
    for (std::size_t i = 0; i < rendered.size(); ++i)
        changed += rendered[i] != empty[i];
    check(changed > 100, "Source-free alpha Game did not draw its particles");
    if (scene_collision) {
        snapshot = captured;
        for (auto& emitter : snapshot.particles) {
            emitter.world_samples.reset();
            emitter.revision.clear();
            emitter.generation = 0;
        }
        check(draw(true) == empty,
              "Stopped Scene response substituted analytic particles instead of requiring Play");
        snapshot = captured;
        check(draw() == rendered, "Live Scene particles did not recover after the stopped view");
    }
    std::filesystem::create_directories(images);
    save_view_png(
        images / (scene_collision_2d ? "xy-scene-particle-collision.png"
                  : scene_collision  ? "scene-particle-collision.png"
                  : sorted           ? "ordered-gpu-particles.png"
                  : gpu && trails    ? "gpu-analytic-particle-trails.png"
                  : gpu && collision ? "gpu-particle-plane-collision.png"
                  : gpu_opacity
                      ? (graph ? "gpu-graph-particle-opacity.png" : "gpu-particle-opacity.png")
                  : weighted
                      ? (graph ? "weighted-gpu-graph-particles.png" : "weighted-gpu-particles.png")
                  : collision  ? "particle-plane-collision.png"
                  : trails     ? "analytic-particle-trails.png"
                  : graph      ? "gpu-graph-camera-particles.png"
                  : gpu        ? "gpu-camera-particles.png"
                  : billboards ? "camera-aligned-particles.png"
                  : opacity    ? "particle-opacity.png"
                               : "sorted-alpha-particles.png"),
        640, 360, {reinterpret_cast<const std::uint8_t*>(rendered.data()), rendered.size() * 4});
    context->WaitForIdle();
    std::cout << "PARTICLE_SOURCE_HIDDEN profile=" << selected->version
              << " emitters=2 changed_pixels=" << changed << '\n';
}
} // namespace forge::test_particle_alpha
