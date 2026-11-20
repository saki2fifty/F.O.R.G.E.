#pragma once
#include "hdr_bloom_gpu_tests.hpp"
#include "mesh_render_host.hpp"
#include "particle_gpu_plan.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#if defined(_WIN32) || defined(FORGE_PARTICLE_PREVIEW_CHECKS)
#include "editor/particle_effect_preview.hpp"
#include "sprite_draw_render_tests.hpp"
#endif
#include <chrono>
#include <forge/game_content.hpp>
#include <forge/runtime_world.hpp>
#include <iostream>
#include <thread>

namespace forge::particle_sorted_content_test {
inline void execute(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& content) {
    using namespace Diligent;
    using test_bloom::check;
#ifdef _WIN32
    const RuntimePackageTarget target{"windows", "d3d12"};
#else
    const RuntimePackageTarget target{"linux", "none"};
#endif
    const auto catalog =
        std::make_shared<const AssetCatalog>(open_runtime_content(content, target));
    AssetId scene;
    std::map<AssetId, ParticleEffectData> values;
    ResourcePool<ParticleEffectAsset> inspection;
    for (const auto& [id, record] : catalog->records()) {
        if (record.type == SceneAsset::type)
            scene = id;
        if (record.type != ParticleEffectAsset::type)
            continue;
        const auto ticket = request_particle_effect(inspection, content, catalog, {id});
        check(inspection.wait(ticket, std::chrono::seconds(15)), "Countercase Effect load failed");
        const auto lease = inspection.acquire(ticket);
        check(bool(lease), "Countercase Effect lease missing");
        values.emplace(id, lease.get());
    }
    check(scene && !std::filesystem::exists(content / "Assets/ParticleFixtures") &&
              !std::filesystem::exists(content / "Assets/Particles"),
          "Countercase runtime retained editable Effect sources");
    RuntimeWorld world({}, {}, std::nullopt, content, false);
    world.scene.restore_snapshot(load_game_scene(content, {scene}));
    const auto particles = particle_runtime(world.engine.world());
    const auto prepare = [&] {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        particles->synchronize();
        while (!world.prepare_physics_assets() || !world.prepare_scene_resources() ||
               !particles->prepare()) {
            particles->synchronize();
            check(std::chrono::steady_clock::now() < end, "Countercase World resources timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    prepare();
    for (unsigned tick = 0; tick < 12; ++tick)
        world.simulation.tick(.25f);
    auto initial = extract_render_scene(world.simulation.presentation(1));
    check(initial.particles.size() == 2 && initial.cameras.size() == 1,
          "Countercase World lost its ordinary emitters or camera");
    const auto base = initial.particles.front().emitter.effect;
    check(initial.particles.back().emitter.effect == base && values.at(base.id).version == 34,
          "Countercase Scene changed the shared explicit Effect selection");
    // Runtime locators name selected cooks, not editable source filenames.
    const auto select_effect = [&](const auto& predicate) {
        AssetRef<ParticleEffectAsset> selected;
        for (const auto& [id, value] : values)
            if (id != base.id && predicate(value)) {
                check(!selected.id, "Countercase Effect selection is ambiguous");
                selected.id = id;
            }
        check(bool(selected.id), "Countercase Effect selection is missing");
        return selected;
    };
    const auto compatible = select_effect([&](const auto& value) {
        return value.version == 34 && value.material == values.at(base.id).material;
    });
    const auto different = select_effect([&](const auto& value) {
        return value.version == 34 && value.material != values.at(base.id).material;
    });
    const auto profile = [&](unsigned version) {
        return select_effect([&](const auto& value) { return value.version == version; });
    };
    const auto cpu = profile(6), weighted = profile(18), plane = profile(36), ribbon = profile(37);
    const std::array actors{initial.particles[0].entity, initial.particles[1].entity};
    const auto select = [&](AssetRef<ParticleEffectAsset> first,
                            AssetRef<ParticleEffectAsset> second) {
        const std::array refs{first, second};
        for (unsigned index = 0; index < actors.size(); ++index) {
            auto entity = world.scene.entity(actors[index].str());
            // Plane Meshes retain emitter parity; this fixture exercises a
            // compatible group independently of the Gallery's mirrored quads.
            entity.set<LocalScale>({1, 1, 1});
            auto emitter = entity.get<ParticleEmitter>();
            emitter.effect = refs[index];
            entity.set<ParticleEmitter>(emitter);
        }
        prepare();
        for (unsigned tick = 0; tick < 4; ++tick)
            world.simulation.tick(.25f);
        auto snapshot = extract_render_scene(world.simulation.presentation(1));
        check(snapshot.diagnostics.empty() && snapshot.particles.size() == 2,
              "Runtime Effect switching lost its prepared typed sources");
        snapshot.settings.shadows.enabled = false;
        return snapshot;
    };
    auto host = std::make_shared<MeshResourceHost>(presentation, context, content);
    host->catalog(catalog);
    MeshSceneRenderer renderer(host, TEX_FORMAT_RGBA32_FLOAT);
    constexpr unsigned width = 96, height = 64;
    TextureDesc desc;
    desc.Name = "FORGE published sorted-group countercase";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA32_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET;
    RefCntAutoPtr<ITexture> color, depth;
    presentation.device()->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    presentation.device()->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Published countercase targets unavailable");
    unsigned frames = 0;
    const auto draw = [&](const RenderScene& snapshot, bool refused) {
        const auto& selected_camera = snapshot.cameras.front();
        const auto camera =
            camera_view(selected_camera.camera, selected_camera.world, width, height);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        std::vector<test_bloom::Pixel> pixels;
        do {
            presentation.begin_frame();
            host->pump();
            renderer.update(snapshot);
            auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            constexpr float background[]{.02f, .04f, .08f, 1};
            context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Viewport viewport{0, 0, float(width), float(height), 0, 1};
            context->SetViewports(1, &viewport, width, height);
            renderer.draw(snapshot, camera, UINT32_MAX, color, dsv);
            presentation.end_frame(context);
            host->submit();
            pixels = test_bloom::read(presentation.device(), context, color);
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < end,
                  "Published countercase rendering timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        if (!refused && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text +
                                     "; published countercase frame " + std::to_string(frames));
        check(!refused || std::any_of(renderer.diagnostics().begin(), renderer.diagnostics().end(),
                                      [](const auto& item) {
                                          return item.text.find("GPU sorted alpha unavailable") !=
                                                 std::string::npos;
                                      }),
              "Published incomplete alpha group lost its refusal diagnostic");
        ++frames;
        return pixels;
    };
    const auto merged = [&](const RenderScene& snapshot) {
        auto empty = snapshot;
        empty.particles.clear();
        const auto baseline = draw(empty, false);
        const auto ordinary = renderer.draw_stats();
        const auto pixels = draw(snapshot, false);
        unsigned slots = 0;
        const auto& selected_camera = snapshot.cameras.front();
        const auto camera =
            camera_view(selected_camera.camera, selected_camera.world, width, height);
        for (const auto& emitter : snapshot.particles) {
            const auto& effect = values.at(emitter.emitter.effect.id);
            const auto input = prepare_particle_gpu_values(
                effect, emitter.time, emitter.emitter.seed, emitter.world, camera,
                emitter.emitter.size_multiplier,
                {emitter.emitter.tint_r, emitter.emitter.tint_g, emitter.emitter.tint_b});
            slots += input.births[0] * (effect.trail ? effect.trail->segments : 1);
        }
        check(slots && renderer.draw_stats().instances == ordinary.instances + slots &&
                  renderer.draw_stats().calls == ordinary.calls + (slots + 63) / 64,
              "Published compatible Effects did not share one bounded sorted transport");
        unsigned visible = 0;
        for (unsigned pixel = 0; pixel < pixels.size(); ++pixel)
            visible += pixels[pixel] != baseline[pixel];
        check(visible > 5, "Published sorted profile produced no visible geometry");
    };
    merged(select(base, compatible));
    for (const auto other : {different, cpu, weighted}) {
        const auto snapshot = select(base, other);
        const auto refused = draw(snapshot, true);
        if (other == different)
            check(std::any_of(renderer.diagnostics().begin(), renderer.diagnostics().end(),
                              [](const auto& item) {
                                  return item.text.find("one shared physical Mesh/Material") !=
                                         std::string::npos;
                              }),
                  "Distinct Material publications were not refused by physical identity");
        auto reference = snapshot;
        std::erase_if(reference.particles, [&](const auto& emitter) {
            return other != cpu || emitter.emitter.effect != cpu;
        });
        check(draw(reference, false) == refused,
              "Published group refusal changed the surviving CPU/ordinary draws");
        merged(select(base, compatible));
    }
    merged(select(plane, plane));
    merged(select(ribbon, ribbon));
#ifdef _WIN32
    const auto graph = profile(35);
    merged(select(base, graph));
    merged(select(graph, graph));
#endif
#if defined(_WIN32) || defined(FORGE_PARTICLE_PREVIEW_CHECKS)
    auto source = ParticleEffectSource::create(AssetId::generate());
    source.document["material"] = values.at(base.id).material;
    source.document["emission"] = "burst";
    source.document["capacity"] = source.document["burst_count"] = 1;
    source.document["speed_min"] = source.document["speed_max"] = 0;
    source.document["acceleration"] = {0, 0, 0};
    source.document["size_start"] = source.document["size_end"] = .5;
    source.enable_gpu_billboards(true);
    source.enable_gpu_opacity(true);
    ParticleEffectPreview preview(presentation, context, content, source.asset());
    const auto preview_draw = [&](double time) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        test_sprite_draw::Pixels pixels;
        do {
            presentation.begin_frame();
            auto* image = preview.render(source, time, catalog, 128, 128);
            check(image, "Particle preview output missing");
            presentation.end_frame(context);
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            check(std::chrono::steady_clock::now() < end, "Particle preview adoption timed out");
            if (preview.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (preview.pending());
        if (!preview.diagnostics().empty())
            throw std::runtime_error(preview.diagnostics().front().text);
        return pixels;
    };
    const auto empty_preview = preview_draw(3);
    for (unsigned mode = 0; mode < 3; ++mode) {
        if (mode == 1)
            source.enable_gpu_sorted_alpha(true);
        if (mode == 2)
            source.enable_gpu_weighted_alpha(true);
        const auto image = preview_draw(1);
        unsigned visible = 0;
        for (unsigned pixel = 0; pixel < image.size(); ++pixel)
            visible += image[pixel] != empty_preview[pixel];
        check(visible > 50, "Changing GPU alpha transport omitted the actual asset preview");
    }
    std::cout << "Actual Effect preview weighted/sorted/weighted transport passed\n";
#endif
    std::cout << "Published compatible/distinct/CPU/weighted/plane/ribbon sorted-group World "
                 "checks passed; frames="
              << frames << '\n';
}
} // namespace forge::particle_sorted_content_test
