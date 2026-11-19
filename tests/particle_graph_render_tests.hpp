#pragma once
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "frame_renderer.hpp"
#include "particle_effect_resource.hpp"
#include "particle_gpu.hpp"
#include "particle_gpu_reference.hpp"
#include "particle_runtime.hpp"
#include "runtime_package.hpp"
#include "shader_diligent.hpp"
#include "sprite_draw_render_tests.hpp"
#include "view_image.hpp"
#include <cmath>
#include <forge/game_content.hpp>
#include <forge/runtime_world.hpp>
#include <forge/shader_resource.hpp>
#include <iostream>
#include <thread>

namespace forge::test_particle_graph_render {
using test_sprite_draw::check;
using namespace Diligent;

// The curated Orbit graph has an independent numeric expectation. This loads
// only selected cooked bytes; it does not regenerate or compile the graph.
inline void compute(DiligentPresentation& presentation, IDeviceContext* context,
                    const std::filesystem::path& root, const AssetCatalog& catalog, AssetId shader,
                    const ParticleGraphProgram& settings) {
    ResourcePool<ShaderAsset> shaders;
    const auto ticket = request_shader(shaders, root, catalog, {shader});
    check(shaders.wait(ticket, std::chrono::seconds(15)), "Cooked graph Shader load stalled");
    const auto lease = shaders.acquire(ticket);
    check(bool(lease), "Cooked graph Shader lease missing");
    validate_particle_graph_shader(lease.get());
    unsigned creations = 0;
    auto program = asset_detail::realize_diligent_shader(
        presentation.device(), lease.get(), [&](const auto& info, IShader** native) {
            check(info.ByteCode && info.ByteCodeSize && !info.Source,
                  "Graph runtime attempted source compilation instead of cooked realization");
            ++creations;
            presentation.shader(info, native);
        });
    check(creations == 1, "Graph runtime did not realize exactly one Compute entry");
    const ParticleGpuGraphLimits limits{{float(settings.extent[0]), float(settings.extent[1]),
                                         float(settings.extent[2]), float(settings.maximum_size)}};
    ParticleGpuInstances instances(
        presentation, 64, program.stages.at({ShaderStage::Compute, ShaderEntryRole::Program}),
        limits);
    ParticleGpuValues values;
    values.births = {64, 0xfffffff0u, 2, 173};
    values.modes[0] = 1;
    values.timing = {.8f, .01f, 2, .2f};
    values.axis = {0, 1, 0, 1};
    values.side = {1, 0, 0, 1};
    values.up = {0, 0, 1, 1};
    values.acceleration = {0, -2, 0, 2};
    values.shape = {1, 2, 3, 0};
    values.sizes = {.1f, .2f, 0, 0};
    values.tint = {.5f, 1, 2, 0};
    values.colors = {ParticleGpuRow{.7f, .8f, .9f, 0}, ParticleGpuRow{.1f, .2f, .3f, 0}};
    values.emitter = {ParticleGpuRow{-2, .3f, 0, 1}, ParticleGpuRow{0, 3, .4f, -2},
                      ParticleGpuRow{0, 0, .5f, 3}};
    instances.evaluate(context, values);
    check(instances.count() == 64, "Cooked graph did not publish its current instances");
    BufferDesc desc;
    desc.Name = "FORGE cooked particle graph verification";
    desc.Size = 64 * 7 * sizeof(ParticleGpuRow);
    desc.Usage = USAGE_STAGING;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<IBuffer> staging;
    presentation.device()->CreateBuffer(desc, nullptr, &staging);
    check(bool(staging), "Cooked graph staging allocation failed");
    context->CopyBuffer(instances.buffer(), 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                        0, desc.Size, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->WaitForIdle();
    MapHelper<ParticleGpuRow> mapped(context, staging, MAP_READ, MAP_FLAG_DO_NOT_WAIT);
    check(static_cast<ParticleGpuRow*>(mapped), "Cooked graph staging map failed");
    double maximum = 0;
    for (unsigned id = 0; id < 64; ++id) {
        const auto ordinal = ((std::uint64_t(values.births[2]) << 32) | values.births[1]) + id;
        particle_gpu_test::Random random(values.births[3], ordinal);
        for (unsigned i = 0; i < 3; ++i)
            (void)random.unit();
        std::array<double, 3> position;
        for (unsigned i = 0; i < 3; ++i)
            position[i] = (random.unit() - .5) * values.shape[i];
        const double age = std::max(0., double(values.timing[0]) - id * double(values.timing[1])),
                     drag = values.timing[3], alpha = -std::expm1(-drag * age) / drag,
                     beta = (age - alpha) / drag, u = age / values.timing[2],
                     phase = age * 4 + random.unit() * 6.283185307179586;
        position[0] += std::cos(phase) * .45;
        position[1] += alpha - 2 * beta;
        position[2] += std::sin(phase) * .45;
        for (unsigned i = 0; i < 3; ++i)
            position[i] = std::clamp(position[i], -double(limits.extent_size[i]),
                                     double(limits.extent_size[i]));
        const double size =
            std::clamp((values.sizes[0] + (values.sizes[1] - values.sizes[0]) * u) * (1 - u), 0.,
                       double(limits.extent_size[3])) *
            values.acceleration[3];
        for (unsigned row = 0; row < 3; ++row) {
            double translation = values.emitter[row][3];
            for (unsigned col = 0; col < 3; ++col) {
                translation += values.emitter[row][col] * position[col];
                maximum = std::max(maximum, std::abs(double(mapped[id * 7 + row][col]) -
                                                     values.emitter[row][col] * size));
            }
            maximum = std::max(maximum, std::abs(double(mapped[id * 7 + row][3]) - translation));
            constexpr double accent[]{.05, .7, 1};
            const double base = values.colors[0][row] +
                                (values.colors[1][row] - values.colors[0][row]) * u,
                         color = (base + (accent[row] - base) * u) * values.tint[row];
            maximum = std::max(maximum, std::abs(double(mapped[id * 7 + 6][row]) - color));
        }
        for (unsigned row = 0; row < 7; ++row)
            for (float value : mapped[id * 7 + row])
                check(std::isfinite(value), "Cooked graph published a nonfinite instance");
    }
    check(maximum < 6e-5, "Cooked Orbit graph differs from its independent numeric reference");
    std::cout << "PARTICLE_GRAPH_COOKED particles=64 max_absolute_error=" << maximum << '\n';
}

inline void exercise(DiligentPresentation& presentation, IDeviceContext* context,
                     const std::filesystem::path& root, const std::filesystem::path& images) {
    const auto catalog =
        std::make_shared<const AssetCatalog>(open_runtime_content(root, {"windows", "d3d12"}));
    AssetId scene, shader, effect;
    for (const auto& [id, record] : catalog->records()) {
        if (record.type == SceneAsset::type)
            scene = id;
        else if (record.type == ShaderAsset::type)
            shader = id;
        else if (record.type == ParticleEffectAsset::type)
            effect = id;
    }
    check(catalog->records().size() == 3 && scene && shader && effect,
          "Native graph content closure is not exactly Scene, Effect and Shader");
    ResourcePool<ParticleEffectAsset> effects;
    const auto ticket = request_particle_effect(effects, root, catalog, {effect});
    check(effects.wait(ticket, std::chrono::seconds(15)), "Native graph Effect load stalled");
    const auto lease = effects.acquire(ticket);
    check(bool(lease) && lease->version == 4 && lease->program &&
              lease->program->shader.id == shader,
          "Native graph Effect lost its selected Shader");
    compute(presentation, context, root, *catalog, shader, *lease->program);

    RuntimeWorld world({}, {}, std::nullopt, root, false);
    world.scene.restore_snapshot(load_game_scene(root, {scene}));
    const auto ready = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!world.prepare_physics_assets() || !world.prepare_scene_resources()) {
        check(std::chrono::steady_clock::now() < ready, "Graph RuntimeWorld preparation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto particles = particle_runtime(world.engine.world());
    while (!particles->prepare()) {
        particles->synchronize();
        check(std::chrono::steady_clock::now() < ready, "Graph fixed-clock Effect load stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (unsigned i = 0; i < 12; ++i)
        world.simulation.tick(.25f);
    auto snapshot = extract_render_scene(world.simulation.presentation(1));
    check(snapshot.particles.size() == 2 && snapshot.cameras.size() == 1 &&
              snapshot.diagnostics.empty(),
          "Source-hidden graph runtime lost its two emitters and Camera");
    for (const auto& emitter : snapshot.particles)
        check(emitter.time >= 2.9 && emitter.time <= 3.1,
              "Graph renderer did not receive the actual fixed-clock particle time");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    host->catalog(catalog);
    FrameRenderer renderer(presentation);
    renderer.resources(host);
    auto draw = [&] {
        test_sprite_draw::Pixels pixels;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        do {
            presentation.begin_frame();
            auto* view = renderer.game(context, snapshot, 640, 360);
            check(view, "Source-hidden graph Game image missing");
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, view->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            if (!renderer.diagnostics().empty())
                throw std::runtime_error(renderer.diagnostics().front().text);
            check(std::chrono::steady_clock::now() < deadline, "Graph Game draw adoption stalled");
        } while (renderer.pending());
        return pixels;
    };
    const auto rendered = draw();
    check(host->inspect().shaders.selected == 1, "Graph renderer did not use its Shader pool");
    snapshot.particles.clear();
    const auto empty = draw();
    unsigned changed = 0;
    for (std::size_t i = 0; i < rendered.size(); ++i)
        changed += rendered[i] != empty[i];
    check(changed > 100, "Cooked graph Game scene did not produce visible particles");
    save_view_png(images / "particle-graphs.png", 640, 360,
                  {reinterpret_cast<const std::uint8_t*>(rendered.data()), rendered.size() * 4});
    context->WaitForIdle();
    std::cout << "PARTICLE_GRAPH_SOURCE_HIDDEN backend=D3D12-WARP emitters=2 changed_pixels="
              << changed << '\n';
}
} // namespace forge::test_particle_graph_render
