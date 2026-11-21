#pragma once
#include "engine_render_resource.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_render_host.hpp"
#include "tile_map_render_tests.hpp"
#ifdef _WIN32
#include "fixture_crash.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <forge/particle_effect.hpp>
#include <iostream>
#include <thread>
#include <utility>

namespace forge::particle_sorted_test {
inline void execute(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& root) {
    using namespace Diligent;
    using test_bloom::check;
    auto* device = presentation.device();
    std::filesystem::create_directories(root);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialData surface;
    surface.model = "forge.gltf.unlit.v1";
    surface.alpha = MaterialAlpha::Blend;
    surface.depth_write = false;
    surface.double_sided = true;
    float alpha = .125f;
    auto publish_material = [&] {
        surface.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                 {1, 1, 1, alpha}};
        host->preview_material(material, {surface, {}, {}, {}});
    };
    publish_material();
    const AssetRef<ParticleEffectAsset> effect{AssetId::generate()};
    ParticleEffectData data;
    data.version = 28;
    data.mesh = engine_primitive(8);
    data.material = material;
    data.capacity = 128;
    data.spawn_rate = 64;
    data.lifetime = 2;
    data.direction = {0, 0, 1};
    data.cone_angle = 0;
    data.speed_min = data.speed_max = 2;
    data.acceleration = {};
    data.size_start = data.size_end = 1;
    data.color_start = data.color_end = {1, 1, 1};
    host->preview_particle_effect(effect, data);
    MeshSceneRenderer renderer(host, TEX_FORMAT_RGBA32_FLOAT);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    const std::array<std::array<float, 3>, 3> tints{
        std::array{.8f, .1f, .2f}, std::array{.2f, .8f, .1f}, std::array{.1f, .2f, .9f}};
    for (unsigned i = 0; i < 3; ++i) {
        RenderParticleEmitter emitter;
        emitter.entity = EntityId::generate();
        emitter.emitter.effect = effect;
        emitter.emitter.tint_r = tints[i][0];
        emitter.emitter.tint_g = tints[i][1];
        emitter.emitter.tint_b = tints[i][2];
        emitter.time = double(i == 2 ? 17 : 65) / 64;
        emitter.world.m[11] = .5 + (i == 1 ? 1. / 64 : i == 2 ? 3. / 128 : 0);
        scene.particles.push_back(emitter);
    }
    constexpr unsigned width = 32, height = 32;
    TextureDesc target;
    target.Name = "FORGE complete-view ordered particle target";
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
    check(color && depth, "Ordered particle targets unavailable");
    auto mismatch_desc = color->GetDesc();
    mismatch_desc.Name = "FORGE intentionally mismatched target";
    mismatch_desc.Format = TEX_FORMAT_RGBA8_UNORM;
    RefCntAutoPtr<ITexture> mismatch;
    device->CreateTexture(mismatch_desc, nullptr, &mismatch);
    check(bool(mismatch), "Mismatched-format fixture target unavailable");
    const std::array<float, 4> background{.02f, .04f, .08f, 1};
    Camera camera;
    camera.projection = 1;
    camera.orthographic_height = 2;
    AffineTransform eye;
    unsigned frames = 0;
    const auto draw = [&](bool diagnostic = false) {
        const auto trace = [&](const char* phase) {
#ifdef _WIN32
            char label[64];
            std::snprintf(label, sizeof(label), "sorted-f%u-%s-m%zu-p%zu-d%u", frames, phase,
                          scene.meshes.size(), scene.particles.size(), unsigned(diagnostic));
            test::fixture_breadcrumb(label);
#else
            (void)phase;
#endif
        };
        trace("prepare-begin");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            host->pump();
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline,
                  "Ordered particle resource preparation timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        trace("prepare-ready");
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        if (!frames)
            for (ITexture* rejected : {static_cast<ITexture*>(nullptr), mismatch.RawPtr()}) {
                bool refused = false;
                try {
                    renderer.draw(scene, camera_view(camera, eye, width, height), UINT32_MAX,
                                  rejected, dsv);
                } catch (const std::exception& error) {
                    refused = std::string_view(error.what()).find("prepared pipeline format") !=
                              std::string_view::npos;
                }
                check(refused, "Scene draw accepted a null or incompatible color attachment");
            }
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, background.data(),
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, float(width), float(height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        trace("draw-begin");
        renderer.draw(scene, camera_view(camera, eye, width, height), UINT32_MAX, color, dsv);
        trace("draw-ready");
        if (!diagnostic && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        check(!diagnostic ||
                  std::any_of(renderer.diagnostics().begin(), renderer.diagnostics().end(),
                              [](const auto& item) {
                                  return item.text.find("GPU sorted alpha unavailable") !=
                                         std::string::npos;
                              }),
              "Incomplete ordered view lost its diagnostic");
        auto pixels = test_bloom::read(device, context, color);
        trace("readback-ready");
        context->FinishFrame();
        device->ReleaseStaleResources();
        trace("finish-ready");
        ++frames;
        return pixels;
    };
    const auto assert_center = [&](const auto& pixels, const auto& expected) {
        for (unsigned y = 14; y < 18; ++y)
            for (unsigned x = 14; x < 18; ++x)
                for (unsigned channel = 0; channel < 4; ++channel)
                    if (!std::isfinite(pixels[y * width + x][channel]) ||
                        std::abs(pixels[y * width + x][channel] - expected[channel]) > 3e-4)
                        throw std::runtime_error(nlohmann::json{
                            {"reason",
                             "Production ordered GPU alpha differs from independent composition"},
                            {"frame", frames},
                            {"pixel", {x, y}},
                            {"actual", pixels[y * width + x]},
                            {"expected", expected}}.dump());
    };
    const auto expected = [&] {
        struct Sample {
            double depth, opacity;
            EntityId entity;
            std::uint64_t birth;
            std::array<float, 3> tint;
        };
        std::vector<Sample> samples;
        const auto view = camera_view(camera, eye, width, height);
        auto cpu = data;
        cpu.version = particle_uses_opacity(data) ? 6 : 2;
        for (const auto& emitter : scene.particles)
            for (const auto& sample :
                 sample_particle_effect(cpu, emitter.time, emitter.emitter.seed)) {
                double distance = 0;
                for (unsigned axis = 0; axis < 3; ++axis)
                    distance += (sample.position[axis] + emitter.world.m[4 * axis + 3] -
                                 view.position[axis]) *
                                view.forward[axis];
                samples.push_back(
                    {distance,
                     sample.opacity,
                     emitter.entity,
                     sample.birth,
                     {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b}});
            }
        std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) {
            if (a.depth != b.depth)
                return a.depth > b.depth;
            if (a.entity != b.entity)
                return a.entity < b.entity;
            return a.birth < b.birth;
        });
        std::array<double, 4> result;
        std::copy(background.begin(), background.end(), result.begin());
        for (const auto& sample : samples) {
            const auto opacity = alpha * sample.opacity;
            for (unsigned channel = 0; channel < 3; ++channel)
                result[channel] = sample.tint[channel] * opacity + result[channel] * (1 - opacity);
            result[3] = opacity + result[3] * (1 - opacity);
        }
        return result;
    };
    const auto original = draw();
    assert_center(original, expected());
    check(renderer.draw_stats().instances == 147 && renderer.draw_stats().calls == 3,
          "Ordered packets did not share one group with64-instance slices");
    std::reverse(scene.particles.begin(), scene.particles.end());
    check(draw() == original, "Emitter insertion order changed ordered GPU pixels");
    eye.m = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 6};
    assert_center(draw(), expected());
    eye = {};
    check(draw() == original, "Second camera retained stale ordered packets");
    data.version = 34;
    data.opacity_start = .75;
    data.opacity_end = .25;
    host->preview_particle_effect(effect, data);
    assert_center(draw(), expected());
    alpha = .25f;
    publish_material();
    const auto changed = draw();
    assert_center(changed, expected());
    check(changed != original, "Material publication failed to replace shared alpha resources");
    scene.settings.weighted_transparency = true;
    assert_center(draw(true), background);
    check(renderer.draw_stats().instances == 0, "Sorted group entered weighted accumulation");
    scene.settings.weighted_transparency = false;
    check(draw() == changed, "Sorted group failed to recover after transport refusal");
    scene.particles.front().emitter.layers = 2;
    assert_center(draw(true), background);
    check(renderer.draw_stats().instances == 0, "Different layer masks admitted a partial group");
    scene.particles.front().emitter.layers = UINT32_MAX;
    check(draw() == changed, "Layer refusal poisoned recovered group");
    RenderMesh middle;
    middle.entity = EntityId::generate();
    middle.renderer.mesh = data.mesh;
    middle.renderer.cast_shadows = false;
    middle.renderer.receive_shadows = false;
    const auto slot = asset_detail::engine_mesh_resource(data.mesh).materials.front().key;
    middle.renderer.materials = {{slot, material}};
    middle.world.m[11] = .3;
    scene.meshes.push_back(middle);
    std::array<float, 4> ordinary;
    for (unsigned c = 0; c < 3; ++c)
        ordinary[c] = alpha + background[c] * (1 - alpha);
    ordinary[3] = 1;
    assert_center(draw(true), ordinary);
    check(renderer.draw_stats().instances == 1,
          "An ordinary alpha Mesh failed to preserve its draw while refusing GPU sorting");
    scene.meshes.clear();
    check(draw() == changed, "Ordinary alpha refusal poisoned recovered group");
    // A terminal invalid source must not silently admit the other emitters.
    scene.particles.front().emitter.effect = {AssetId::generate()};
    assert_center(draw(true), background);
    check(renderer.draw_stats().instances == 0,
          "Missing particle resources admitted a partial sorted group");
    scene.particles.front().emitter.effect = effect;
    check(draw() == changed, "Missing-source refusal poisoned recovered group");
    const auto retained = scene.particles;
    scene.particles.clear();
    assert_center(draw(), background);
    check(renderer.draw_stats().instances == 0, "An empty view drew stale ordered packets");
    scene.particles = retained;
    check(draw() == changed, "An empty view poisoned recovered ordering");
    // Use the established runtime Tile DTO fixture and texture owner. No second
    // unsaved Particle Effect or alternate asset registry is needed for2D scope.
    MaterialResourceData textured{surface, {}, {}, {}};
    auto& image_slot = textured.values.textures["baseColorTexture"];
    image_slot.semantic = TextureSemantic::Color;
    image_slot.sampler.min = image_slot.sampler.mag = TextureFilter::Nearest;
    textured.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(material, textured);
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = material;
    sprite.renderer.columns = sprite.renderer.rows = 4;
    sprite.world.m[11] = .4;
    const auto set_id = AssetId::generate(), map_id = AssetId::generate();
    const nlohmann::json set_values{
        {"asset_id", set_id},
        {"images",
         {{{"id", 17},
           {"name", "Shared checker"},
           {"material", material},
           {"grid",
            {{"columns", 4},
             {"rows", 4},
             {"margin_x", 0},
             {"margin_y", 0},
             {"spacing_x", 0},
             {"spacing_y", 0}}}}}},
        {"tiles", {{{"id", 9}, {"name", "One cell"}, {"image", 17}, {"frame", 0}}}}};
    RenderTileMap map;
    map.entity = EntityId::generate();
    map.renderer.map = {map_id};
    map.world.m[3] = map.world.m[7] = -.5;
    map.world.m[11] = .4;
    map.runtime_instance = true;
    const std::array<TileMapCellEdit, 1> cell{{{1, {0, 0}, {9, 0}}}};
    map.cells =
        std::make_shared<const TileMapData>(test_tile_map_render::cells(map_id, set_id, cell));
    map.definitions = std::make_shared<const TileSetData>(
        decode_cooked_tile_set(asset_detail::encode_envelope(set_values, {}, tile_set_cooked_magic,
                                                             tile_set_cooked_metadata_limit),
                               set_id));
    for (unsigned mode = 0; mode < 3; ++mode) {
        scene.sprites = mode != 1 ? std::vector{sprite} : std::vector<RenderSprite>{};
        scene.tile_maps = mode != 0 ? std::vector{map} : std::vector<RenderTileMap>{};
        const auto particles = std::exchange(scene.particles, {});
        const auto only2d = draw();
        const auto instances = renderer.draw_stats().instances;
        check(instances == (mode == 2 ? 2 : 1),
              "2D countercase did not draw its Sprite/Tile cells");
        scene.particles = particles;
        check(draw(true) == only2d && renderer.draw_stats().instances == instances,
              "GPU group refusal changed valid Sprite/Tile drawing or retained sorted particles");
    }
    scene.sprites.clear();
    scene.tile_maps.clear();
    host->preview_material(material, {surface, {}, {}, {}});
    check(draw() == changed, "2D group refusal poisoned subsequent ordinary ordered particles");
    // Compare lit static packets with the existing per-particle CPU renderer.
    // Deterministic point/cone-zero/fixed-speed motion makes both samplers agree;
    // this is a transport/shading oracle, not a claim of matching random streams.
    surface.model = "forge.gltf.metallic-roughness.v1";
    surface.parameters["metallicFactor"] = {MaterialParameterType::Scalar, {0}};
    surface.parameters["roughnessFactor"] = {MaterialParameterType::Scalar, {.8f}};
    publish_material();
    Light light;
    light.intensity = 2;
    scene.lights.push_back({EntityId::generate(), light_view(light, {}), {}, {}});
    for (bool reflected : {false, true})
        for (bool flipped : {false, true}) {
            for (auto& emitter : scene.particles) {
                emitter.world.m[0] = reflected ? -1 : 1;
                emitter.world.m[1] = .2;
            }
            camera.flip_x = flipped;
            data.version = 32;
            host->preview_particle_effect(effect, data);
            const auto gpu = draw();
            check(renderer.draw_stats().instances == 147 && renderer.draw_stats().calls == 3,
                  "Lit GPU group lost bounded instance transport");
            data.version = 6;
            host->preview_particle_effect(effect, data);
            const auto cpu = draw();
            check(renderer.draw_stats().instances == 147 && renderer.draw_stats().calls == 147,
                  "Lit CPU reference did not draw independently sorted particles");
            const auto resources = renderer.draw_stats();
            check(resources.binding_commits >= 1 && resources.binding_commits <= 6 &&
                      resources.binding_commits + resources.binding_reuses == 147,
                  "Independent CPU draws exhausted transient descriptors instead of reusing "
                  "unchanged resources");
            std::cout << "Independent lit CPU draws:147 resource commits="
                      << resources.binding_commits << " reuses=" << resources.binding_reuses
                      << '\n';
            unsigned visible = 0;
            for (unsigned pixel = 0; pixel < gpu.size(); ++pixel) {
                visible += std::abs(cpu[pixel][0] - background[0]) > .001f;
                for (unsigned channel = 0; channel < 4; ++channel)
                    check(std::isfinite(gpu[pixel][channel]) &&
                              std::abs(gpu[pixel][channel] - cpu[pixel][channel]) <= 3e-4,
                          "Ordered GPU PBR shading differs from independently drawn CPU particles");
            }
            check(visible > 20, "Lit particle reference contains no visible geometry");
        }
    std::cout << "Production ordered GPU particle composition, slicing, cameras, source/Material "
                 "changes, complete-group refusal and recovery passed; frames="
              << frames << '\n';
}
} // namespace forge::particle_sorted_test
