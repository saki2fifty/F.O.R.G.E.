#pragma once
#include "engine_render_resource.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_render_host.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>
namespace forge::test_particle_billboard {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& root) {
    using namespace Diligent;
    using test_bloom::check;
    std::filesystem::create_directories(root);
    auto* device = presentation.device();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialResourceData surface;
    auto& values = surface.values;
    values.model = "forge.gltf.unlit.v1";
    values.double_sided = false;
    values.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                            {.8f, .2f, .1f, 1}};
    host->preview_material(material, surface);
    const AssetRef<ParticleEffectAsset> effect{AssetId::generate()};
    ParticleEffectData data;
    data.version = 7;
    data.mesh = engine_primitive(8);
    data.material = material;
    data.emission = ParticleEmission::Burst;
    data.capacity = data.burst_count = 2;
    data.speed_min = data.speed_max = 0;
    data.acceleration = {0, 0, 0};
    data.size_start = data.size_end = 1;
    data.color_start = data.color_end = {1, 1, 1};
    host->preview_particle_effect(effect, data);
    MeshSceneRenderer renderer(host);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    RenderParticleEmitter emitter;
    emitter.entity = EntityId::generate();
    emitter.emitter.effect = effect;
    emitter.time = 1;
    // Source reflection/shear and zero Z scale must not flatten the side view.
    emitter.world.m = {-2, .5, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    scene.particles = {emitter};
    constexpr unsigned width = 64, height = 64;
    TextureDesc target;
    target.Name = "FORGE camera-plane particle numeric target";
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
    check(color && depth, "Camera-plane numeric targets unavailable");
    auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            host->pump();
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline, "Camera-plane resources timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
    };
    unsigned frames = 0;
    auto draw = [&](const Camera& camera, const AffineTransform& world, bool diagnostic = false) {
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]{};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, float(width), float(height), 0, 1};
        context->SetViewports(1, &viewport, width, height);
        renderer.draw(scene, camera_view(camera, world, width, height), camera.layers, color, dsv);
        if (!diagnostic && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        check(!diagnostic || !renderer.diagnostics().empty(),
              "Expected camera-plane diagnostic disappeared");
        auto pixels = test_bloom::read(device, context, color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        ++frames;
        return pixels;
    };
    auto center = [&](const auto& pixels, float factor) {
        const std::array<float, 3> rgb{.8f, .2f, .1f};
        for (unsigned y = 30; y < 34; ++y)
            for (unsigned x = 30; x < 34; ++x)
                for (unsigned channel = 0; channel < 3; ++channel)
                    if (std::abs(pixels[y * width + x][channel] - rgb[channel] * factor) > 2e-4f)
                        throw std::runtime_error(nlohmann::json{
                            {"reason", "Single-sided camera-plane pixel differs"},
                            {"frame", frames},
                            {"pixel", {x, y}},
                            {"actual", pixels[y * width + x]},
                            {"factor", factor}}.dump());
    };
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 4;
    AffineTransform front, side, roll;
    front.m[11] = -3;
    side.m = {0, 0, 1, -3, 0, 1, 0, 0, -1, 0, 0, 0};
    roll.m = {.8, -.6, 0, 0, .6, .8, 0, 0, 0, 0, 1, -3};
    settle();
    const auto reference = draw(camera, front);
    center(reference, 1);
    check(renderer.draw_stats().instances == 2 && renderer.draw_stats().calls == 1 &&
              renderer.draw_stats().batched_calls == 1,
          "Opaque camera-plane births did not use existing instance batching");
    // Draw repeatedly without update: all views share the same retained source poses.
    for (const auto& world : {side, roll, front}) {
        const auto pixels = draw(camera, world);
        center(pixels, 1);
        check(pixels == reference, "Camera change mutated source poses or changed quad dimensions");
    }
    for (unsigned basis : {0u, 1u})
        for (unsigned flips = 0; flips < 4; ++flips) {
            camera.basis = basis;
            camera.flip_x = bool(flips & 1);
            camera.flip_y = bool(flips & 2);
            auto world = front;
            if (basis)
                world.m[11] = 3;
            center(draw(camera, world), 1);
        }
    camera.basis = 0;
    camera.flip_x = camera.flip_y = false;
    // From this side camera the emitter's original Z extent is zero and its
    // center is outside the frustum. The aligned quad still crosses its edge.
    scene.particles.front().world.m[11] = 2.5;
    settle();
    const auto edge = draw(camera, side);
    check(std::count_if(edge.begin(), edge.end(),
                        [](const auto& pixel) { return pixel[0] > .1f; }) > 16,
          "Instance or part culling discarded an aligned quad crossing the frustum edge");
    camera.projection = unsigned(CameraProjection::Perspective);
    const auto perspective = draw(camera, side);
    check(std::count_if(perspective.begin(), perspective.end(),
                        [](const auto& pixel) { return pixel[0] > .1f; }) > 16,
          "Perspective culling discarded an aligned quad crossing the frustum edge");
    scene.particles.front().world.m[11] = 0;
    camera.projection = unsigned(CameraProjection::Orthographic);
    settle();
    // The opacity path is the same sorted queue and packet as mesh particles.
    values.alpha = MaterialAlpha::Blend;
    values.depth_write = false;
    host->preview_material(material, surface);
    data.opacity_start = data.opacity_end = .5;
    host->preview_particle_effect(effect, data);
    settle();
    center(draw(camera, side), .75f);
    check(renderer.draw_stats().instances == 2 && renderer.draw_stats().calls == 2 &&
              !renderer.draw_stats().batched_calls,
          "Transparent camera-plane births were merged into an unordered batch");
    data.opacity_start = data.opacity_end = 0;
    host->preview_particle_effect(effect, data);
    settle();
    center(draw(camera, front), 0);
    // An unsupported resource must disappear, then recover through the same owner.
    values.alpha = MaterialAlpha::Opaque;
    host->preview_material(material, surface);
    settle();
    center(draw(camera, front, true), 0);
    values.alpha = MaterialAlpha::Blend;
    host->preview_material(material, surface);
    data.opacity_start = data.opacity_end = 1;
    host->preview_particle_effect(effect, data);
    settle();
    center(draw(camera, side), 1);
    // An offset, nonintegral checker region distinguishes horizontal/vertical
    // mirroring. The engine image uses nearest texels and no alpha attenuation.
    values.alpha = MaterialAlpha::Opaque;
    values.depth_write = true;
    auto& slot = values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = TextureFilter::Nearest;
    slot.offset = {.13f, .07f};
    slot.scale = {.6f, .35f};
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(material, surface);
    settle();
    const auto textured = draw(camera, front);
    check(std::count_if(textured.begin(), textured.end(),
                        [](const auto& p) { return p[0] > .5f; }) > 40,
          "Textured camera-plane quads lost their source image");
    for (const auto& world : {side, roll})
        check(draw(camera, world) == textured,
              "Changing cameras mirrored or rotated camera-plane texture coordinates");
    for (unsigned basis : {0u, 1u})
        for (unsigned flips = 0; flips < 4; ++flips) {
            camera.basis = basis;
            camera.flip_x = bool(flips & 1);
            camera.flip_y = bool(flips & 2);
            auto world = front;
            if (basis)
                world.m[11] = 3;
            const auto pixels = draw(camera, world);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x) {
                    const auto expected = textured[(camera.flip_y ? height - 1 - y : y) * width +
                                                   (camera.flip_x ? width - 1 - x : x)];
                    for (unsigned channel = 0; channel < 4; ++channel)
                        check(std::abs(pixels[y * width + x][channel] - expected[channel]) < 2e-4f,
                              "Camera-plane UVs or lens flips differ from the independently "
                              "mirrored image");
                }
        }
    std::ofstream receipt(root / "numeric-native.json");
    receipt << nlohmann::json{{"format", "forge.particle-billboard-numeric"},
                              {"version", 1},
                              {"frames", frames},
                              {"single_sided", true},
                              {"view_bases", 2},
                              {"lens_flip_combinations", 4},
                              {"frustum_edge", true},
                              {"perspective", true},
                              {"cross_view_retention", true},
                              {"texture_orientation", true},
                              {"opaque_batching", true},
                              {"opacity", true},
                              {"recovery", true}}
                   .dump(2)
            << '\n';
    check(bool(receipt), "Camera-plane numeric receipt failed");
}
} // namespace forge::test_particle_billboard
