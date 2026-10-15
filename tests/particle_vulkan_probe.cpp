#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "mesh_render_host.hpp"
#include "particle_effect_authoring.hpp"
#include "particle_runtime.hpp"
#include "runtime_entity_creation.hpp"
#include <forge/animation.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/physics.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
void check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void publish(AssetImportService& service, const ParticleEffectDocument& document) {
    service.submit(
        service.prepare(document.locator(), {}, document.source().asset()),
        [](auto& candidate, const auto& plan, const auto&) {
            prepare_particle_effect_publication(candidate, plan);
        },
        [](const auto&, const auto&) {});
    check(service.wait_idle(std::chrono::seconds(15)), "Native particle cook timed out");
    const auto result = service.poll();
    check(result.size() == 1 && result.front().published,
          result.empty() ? "Native particle publication receipt missing"
                         : result.front().diagnostic);
}
struct Target {
    RefCntAutoPtr<ITexture> color, depth;
    Target(IRenderDevice* device, unsigned width, unsigned height) {
        TextureDesc desc;
        desc.Name = "FORGE native particle color";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device->CreateTexture(desc, nullptr, &color);
        desc.Name = "FORGE native particle depth";
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(desc, nullptr, &depth);
        check(color && depth, "Native particle targets unavailable");
    }
};
std::vector<std::array<std::uint8_t, 4>> read(IRenderDevice* device, IDeviceContext* context,
                                              ITexture* texture) {
    auto desc = texture->GetDesc();
    desc.Name = "FORGE native particle readback";
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    check(bool(staging), "Native particle readback texture unavailable");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture({texture, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    check(mapped.pData != nullptr, "Native particle readback map failed");
    std::vector<std::array<std::uint8_t, 4>> pixels(std::size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + y * desc.Width,
                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                    std::size_t(desc.Width) * 4);
    context->UnmapTextureSubresource(staging, 0, 0);
    return pixels;
}
std::size_t visible(const std::vector<std::array<std::uint8_t, 4>>& pixels) {
    return std::count_if(pixels.begin(), pixels.end(),
                         [](const auto& value) { return value[0] + value[1] + value[2] > 12; });
}
std::size_t dominant(const std::vector<std::array<std::uint8_t, 4>>& pixels, unsigned channel) {
    return std::count_if(pixels.begin(), pixels.end(), [channel](const auto& pixel) {
        return pixel[channel] > 20 && pixel[channel] > pixel[(channel + 1) % 3] + 8 &&
               pixel[channel] > pixel[(channel + 2) % 3] + 8;
    });
}
void image(const std::filesystem::path& path, unsigned width, unsigned height,
           const std::vector<std::array<std::uint8_t, 4>>& pixels) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto& pixel : pixels)
        out.write(reinterpret_cast<const char*>(pixel.data()), 3);
    check(bool(out.flush()), "Native particle evidence image write failed");
}
void execute(const std::filesystem::path& root, const std::filesystem::path& gallery = {}) {
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    auto document = ParticleEffectDocument::create(lease, "Assets/Native.particles.json");
    document->edit(document->revision(), "Native original colors/motion", [](auto& source) {
        source["acceleration"] = {0., 0., 0.};
        source["cone_angle"] = 80.;
        source["size_start"] = source["size_end"] = .25;
        source["color_start"] = {1., .1, 0.};
        source["color_end"] = {0., .1, 1.};
    });
    document->save();
    AssetImportService importer(lease, particle_effect_import_registry(), {"linux", "none", "cpu"});
    publish(importer, *document);
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo create;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
    check(device && context, "Native particle Vulkan device/context unavailable");
    std::cout << "Particle native adapter: " << device->GetAdapterInfo().Description << '\n';
    DiligentPresentation presentation(device);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
    MeshSceneRenderer renderer(host);
    auto* active_renderer = &renderer;
    auto active_host = host;
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    RenderScene scene;
    scene.scene = AssetId::generate();
    RenderParticleEmitter emitter;
    emitter.entity = EntityId::generate();
    emitter.emitter.effect = {document->source().asset()};
    emitter.emitter.seed = 8;
    scene.particles.push_back(emitter);
    Camera camera;
    camera.projection = 1;
    camera.orthographic_height = 20;
    AffineTransform camera_world;
    camera_world.m[7] = 4;
    camera_world.m[11] = -20;
    auto admit = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            active_host->pump();
            active_renderer->update(scene);
            if (!active_renderer->diagnostics().empty())
                throw std::runtime_error(active_renderer->diagnostics().front().text);
            check(std::chrono::steady_clock::now() < deadline,
                  "Native particle draw adoption timed out");
            if (active_renderer->pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (active_renderer->pending());
    };
    auto draw = [&](Target& target, std::uint32_t layers = UINT32_MAX) {
        admit();
        auto* color = target.color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* depth = target.depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &color, depth, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[4]{0, 0, 0, 1};
        context->ClearRenderTarget(color, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(depth, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const auto& size = target.color->GetDesc();
        Viewport viewport{0, 0, float(size.Width), float(size.Height), 0, 1};
        context->SetViewports(1, &viewport, size.Width, size.Height);
        active_renderer->draw(scene, camera_view(camera, camera_world, size.Width, size.Height),
                              layers, target.color, depth);
        check(active_renderer->diagnostics().empty(), "Native particle draw reported an error");
        active_host->submit();
        context->Flush();
        return read(device, context, target.color);
    };
    Target square(device, 256, 256);
    scene.particles[0].time = 0;
    check(visible(draw(square)) == 0 && renderer.draw_stats().instances == 0,
          "Continuous particles drew before first birth");
    scene.particles[0].time = 1;
    const auto colored = draw(square);
    std::cout << "Continuous native visible=" << visible(colored)
              << " instances=" << renderer.draw_stats().instances
              << " calls=" << renderer.draw_stats().calls
              << " batched=" << renderer.draw_stats().batched_calls
              << " bundles=" << renderer.bundle_count() << '\n';
    check(visible(colored) > 10 && renderer.draw_stats().instances == 32 &&
              renderer.draw_stats().calls == 1 && renderer.draw_stats().batched_calls == 1,
          "Native particle instances were absent or were expanded into individual draw calls");
    check(dominant(colored, 0) > 20, "Particle instance RGB tint was absent from native pixels");
    image(root / "continuous.ppm", 256, 256, colored);
    // One entity may own an ordinary mesh and an emitter without replacing either cache entry.
    RenderMesh ordinary;
    ordinary.entity = emitter.entity;
    ordinary.renderer.mesh = document->source().compiled().mesh;
    ordinary.renderer.cast_shadows = ordinary.renderer.receive_shadows = false;
    ordinary.world.m[3] = -3;
    scene.meshes.push_back(ordinary);
    draw(square);
    check(renderer.draw_stats().instances == 33,
          "Emitter draw scope overwrote the same entity's ordinary mesh");
    scene.meshes.clear();
    scene.particles[0].emitter.visible = false;
    check(visible(draw(square)) == 0 && renderer.draw_stats().instances == 0,
          "Hidden particles retained visible instances");
    scene.particles[0].emitter.visible = true;
    scene.particles[0].emitter.layers = 2;
    check(visible(draw(square, 1)) == 0, "Particle layer filter was ignored");
    scene.particles[0].emitter.layers = UINT32_MAX;
    Target resized(device, 320, 192);
    check(visible(draw(resized)) > 10 && renderer.draw_stats().instances == 32,
          "Native particle resize changed emission or lost its resource bundle");
    document->edit(document->revision(), "Full bounded particle batch", [](auto& source) {
        source["capacity"] = 1024;
        source["spawn_rate"] = 256.;
        source["lifetime"] = 4.;
        source["color_start"] = source["color_end"] = {0., .1, 1.};
    });
    document->save();
    publish(importer, *document);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    scene.particles[0].time = 4;
    const auto bounded = draw(square);
    check(renderer.draw_stats().instances == 1024 &&
              renderer.draw_stats().calls == 1024 / MeshDraw::instance_limit &&
              renderer.bundle_count() == 1 && renderer.bounds().has_value() &&
              dominant(bounded, 2) > 20,
          "Native maximum effect lost shared bounded batches/bounds or published blue tint");
    image(root / "bounded.ppm", 256, 256, bounded);
    const auto full = scene.particles.front();
    for (unsigned i = 1; i < particle_scene_capacity / 1024; ++i) {
        auto added = full;
        added.entity = EntityId::generate();
        scene.particles.push_back(added);
    }
    draw(square);
    check(renderer.draw_stats().instances == particle_scene_capacity &&
              renderer.draw_stats().calls == particle_scene_capacity / MeshDraw::instance_limit &&
              renderer.bundle_count() == 1,
          "Native scene capacity boundary lost instancing or shared bundle reuse");
    auto excess = full;
    excess.entity = EntityId::generate();
    scene.particles.push_back(excess);
    renderer.update(scene);
    check(!renderer.diagnostics().empty(), "Native reserved scene capacity silently overflowed");
    scene.particles.resize(1);
    camera_world.m[3] = 1000;
    check(visible(draw(square)) == 0 && renderer.draw_stats().instances == 0,
          "Off-camera particle instances bypassed camera culling");
    camera_world.m[3] = 0;
    const auto published = AssetCatalog::open_project(root);
    const auto selected =
        published.records().at(document->source().asset()).metadata.at("forge.import");
    scene.particles[0].revision = std::string(64, '0');
    scene.particles[0].generation = selected.at("generation");
    host->pump();
    renderer.update(scene);
    check(!renderer.diagnostics().empty(),
          "Stale runtime effect selection reached the native draw");
    scene.particles[0].revision.clear();
    scene.particles[0].generation = 0;
    document->edit(document->revision(), "Initial burst lifetime", [](auto& source) {
        source["emission"] = "burst";
        source["burst_count"] = 16;
        source["lifetime"] = .25;
    });
    document->save();
    publish(importer, *document);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    scene.particles[0].time = 0;
    check(visible(draw(square)) > 0 && renderer.draw_stats().instances == 16,
          "Initial burst did not render at birth time");
    scene.particles[0].time = .25;
    check(visible(draw(square)) == 0 && renderer.draw_stats().instances == 0,
          "Expired burst retained native particle instances");
    const auto old = scene.particles[0];
    for (unsigned i = 1; i <= particle_emitter_limit; ++i) {
        auto added = old;
        added.entity = EntityId::generate();
        scene.particles.push_back(added);
    }
    bool rejected = false;
    try {
        renderer.update(scene);
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Native renderer accepted more than64 enabled emitters");
    // Detached draft preview renders through the same mesh path, without
    // publishing a new cook or changing the selected/source revision.
    scene.particles.resize(1);
    scene.particles[0].time = 0;
    const auto saved_source = document->source().document;
    const auto saved_selection =
        AssetCatalog::open_project(root).records().at(document->source().asset()).metadata;
    auto draft = document->source().compiled();
    draft.color_start = draft.color_end = {0, 1, 0};
    draft.size_start = draft.size_end = .5;
    bool override_rejected = false;
    try {
        host->preview_particle_effect(emitter.emitter.effect, draft);
    } catch (const std::exception&) {
        override_rejected = true;
    }
    check(override_rejected, "Ordinary runtime host accepted unsaved particle values");
    auto preview_host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    preview_host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
    preview_host->preview_particle_effect(emitter.emitter.effect, draft);
    MeshSceneRenderer preview_renderer(preview_host);
    active_host = preview_host;
    active_renderer = &preview_renderer;
    const auto preview_pixels = draw(square);
    check(dominant(preview_pixels, 1) > 20 && preview_renderer.draw_stats().instances == 16,
          "Unsaved isolated preview did not render its actual draft color/instances");
    ParticleEffectDocument reopened(lease, document->locator());
    check(reopened.source().document == saved_source &&
              AssetCatalog::open_project(root).records().at(document->source().asset()).metadata ==
                  saved_selection,
          "Unsaved particle preview mutated source or selected catalog revision");
    image(root / "draft-preview.ppm", 256, 256, preview_pixels);
    draft.material = engine_material();
    Light sun;
    sun.intensity = 3;
    sun.cast_shadows = false;
    scene.lights = {{EntityId::generate(), light_view(sun, {})}};
    preview_host->preview_particle_effect(emitter.emitter.effect, draft);
    const auto pbr_pixels = draw(square);
    check(dominant(pbr_pixels, 1) > 20 && preview_renderer.draw_stats().instances == 16,
          "Built-in PBR particles lost per-instance RGB tint under actual lighting");
    image(root / "draft-preview-pbr.ppm", 256, 256, pbr_pixels);
    // Extended values travel through the real private MeshSceneRenderer path.
    // Pixel coverage and independent image centroids distinguish origin/motion;
    // counting CPU samples alone would not prove the GPU consumed these values.
    auto extended = draft;
    extended.material = engine_material(EngineMaterial::LegacyBlockout);
    extended.capacity = extended.burst_count = 96;
    extended.lifetime = 4;
    extended.cone_angle = 0;
    extended.speed_min = extended.speed_max = 4;
    extended.acceleration = {0, 0, 0};
    extended.shape_size = {8, 4, 6};
    extended.shape_radius = 3;
    extended.size_curve = {{0, .15, false}, {.4, .45, false}, {1, .08, false}};
    extended.color_gradient = {{0, {1, 0, 0}, false}, {.5, {0, 1, 0}, true}, {1, {0, 0, 1}, false}};
    std::size_t point_coverage = 0;
    for (unsigned shape = 0; shape < 4; ++shape) {
        extended.shape = ParticleSpawnShape(shape);
        scene.particles[0].time = 0;
        preview_host->preview_particle_effect(emitter.emitter.effect, extended);
        const auto pixels = draw(square);
        check(preview_renderer.draw_stats().instances == 96 && dominant(pixels, 0) > 0,
              "Extended origin lost native instances or birth gradient color");
        if (shape == 0)
            point_coverage = visible(pixels);
        else
            check(visible(pixels) > point_coverage * 10,
                  "Native spawn volume collapsed to a point despite admitted shape data");
        image(root / ("shape-" + std::to_string(shape) + ".ppm"), 256, 256, pixels);
    }
    extended.shape = ParticleSpawnShape::Point;
    auto centroid_y = [&](const auto& pixels) {
        double sum = 0;
        std::size_t count = 0;
        for (std::size_t i = 0; i < pixels.size(); ++i)
            if (pixels[i][0] + pixels[i][1] + pixels[i][2] > 12) {
                sum += i / 256;
                ++count;
            }
        check(count != 0, "Extended motion centroid has no native pixels");
        return sum / count;
    };
    scene.particles[0].time = 1.6;
    preview_host->preview_particle_effect(emitter.emitter.effect, extended);
    const auto free_pixels = draw(square);
    check(visible(free_pixels) > point_coverage * 2,
          "Native size curve did not grow the actual particle geometry");
    extended.drag = 2;
    preview_host->preview_particle_effect(emitter.emitter.effect, extended);
    const auto dragged_pixels = draw(square);
    check(std::abs(centroid_y(free_pixels) - centroid_y(dragged_pixels)) > 40,
          "Native drag did not change image-space motion relative to the free jet");
    image(root / "curve-free.ppm", 256, 256, free_pixels);
    image(root / "curve-drag.ppm", 256, 256, dragged_pixels);
    scene.particles[0].time = 3;
    const auto stepped_pixels = draw(square);
    check(dominant(stepped_pixels, 1) > 0 && dominant(stepped_pixels, 0) == 0 &&
              dominant(stepped_pixels, 2) == 0,
          "Native lifetime gradient did not hold the green outgoing Step segment");
    image(root / "gradient-step.ppm", 256, 256, stepped_pixels);
    extended.burst_count = 1;
    extended.speed_min = extended.speed_max = 0;
    extended.acceleration = {0, 0, 0};
    extended.size_curve.clear();
    extended.color_gradient.clear();
    extended.size_start = extended.size_end = .4;
    extended.color_start = extended.color_end = {1, 1, 1};
    preview_host->preview_particle_effect(emitter.emitter.effect, extended);
    scene.particles[0].time = .5;
    scene.particles[0].emitter.size_multiplier = 1;
    const auto neutral = draw(square);
    scene.particles[0].emitter.size_multiplier = 4;
    const auto large = draw(square);
    check(visible(neutral) > 0 && visible(large) > visible(neutral) * 8 &&
              preview_renderer.draw_stats().instances == 1 && preview_renderer.bundle_count() == 1,
          "Native emitter size did not change geometry without copying its bundle");
    image(root / "emitter-size-neutral.ppm", 256, 256, neutral);
    image(root / "emitter-size-large.ppm", 256, 256, large);
    scene.particles[0].emitter.tint_r = 1;
    scene.particles[0].emitter.tint_g = scene.particles[0].emitter.tint_b = 0;
    const auto red = draw(square);
    check(dominant(red, 0) > 20 && dominant(red, 1) == 0 && dominant(red, 2) == 0 &&
              visible(red) == visible(large),
          "Native emitter tint changed geometry or failed to multiply RGB");
    scene.particles[0].world.m[3] = -1.5;
    auto shared = scene.particles[0];
    shared.entity = EntityId::generate();
    shared.world.m[3] = 1.5;
    shared.emitter.size_multiplier = 2;
    shared.emitter.tint_r = 0;
    shared.emitter.tint_g = 1;
    scene.particles.push_back(shared);
    const auto pair = draw(square);
    check(dominant(pair, 0) > 20 && dominant(pair, 1) > 20 &&
              preview_renderer.draw_stats().instances == 2 && preview_renderer.bundle_count() == 1,
          "Distinct emitter appearances duplicated their effect bundle or lost native RGB");
    image(root / "emitter-shared-appearances.ppm", 256, 256, pair);
    camera_world.m[3] = 1000;
    check(visible(draw(square)) == 0 && preview_renderer.draw_stats().instances == 0,
          "Extended volume/curve preview bypassed ordinary camera culling");
    camera_world.m[3] = 0;
    check(reopened.source().document == saved_source &&
              AssetCatalog::open_project(root).records().at(document->source().asset()).metadata ==
                  saved_selection,
          "Extended private previews changed source or selected catalog metadata");
    if (!gallery.empty())
        for (unsigned number : {60u, 61u}) {
            const bool curves = number == 61;
            const auto scene_path =
                gallery / (curves ? "Scenes/61-animated-property-curves.scene.json"
                                  : "Scenes/60-animated-particle-appearance.scene.json");
            if (!std::filesystem::exists(scene_path))
                continue;
            Json source;
            std::ifstream(scene_path) >> source;
            EngineContext engine(WorldRole::Runtime, false,
                                 {animation_module(gallery), particle_module(gallery)});
            Scene live(engine.world());
            live.restore_snapshot(source);
            RuntimeSimulation simulation(engine.world(), live);
            auto animation = animation_runtime(engine.world());
            auto particles = particle_runtime(engine.world());
            const auto catalog =
                std::make_shared<const AssetCatalog>(AssetCatalog::open_project(gallery));
            animation->catalog(catalog);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!animation->prepare_initial_pose() || !particles->prepare()) {
                check(std::chrono::steady_clock::now() < deadline,
                      "Native Gallery60 resources timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            simulation.reset_presentation();
            auto gallery_host = std::make_shared<MeshResourceHost>(presentation, context, gallery);
            gallery_host->catalog(catalog);
            MeshSceneRenderer gallery_renderer(gallery_host);
            active_host = gallery_host;
            active_renderer = &gallery_renderer;
            Target wide(device, 960, 540);
            std::vector<std::array<std::uint8_t, 4>> first;
            for (unsigned stage = 0; stage < 2; ++stage) {
                simulation.tick(.25f);
                if (!curves || stage)
                    simulation.tick(.25f);
                scene = extract_render_scene(simulation.presentation(1));
                check(scene.diagnostics.empty() && scene.cameras.size() == 1 &&
                          scene.particles.size() == 2,
                      "Native animated particle gallery capture failed");
                if (curves && stage == 0) {
                    std::array sizes{scene.particles[0].emitter.size_multiplier,
                                     scene.particles[1].emitter.size_multiplier};
                    std::sort(sizes.begin(), sizes.end());
                    check(std::abs(sizes[0] - .734375) < 1e-9 && std::abs(sizes[1] - .875) < 1e-9,
                          "Native source4 scalar curve capture retained linear timing");
                }
                camera = scene.cameras.front().camera;
                camera_world = scene.cameras.front().world;
                scene.meshes.clear();
                // Freeze geometry age to isolate the actual keyframed appearance.
                for (auto& particle : scene.particles)
                    particle.time = .75;
                const auto pixels = draw(wide);
                image(root / ("gallery" + std::to_string(number) + "-phase" +
                              std::to_string(stage) + ".ppm"),
                      960, 540, pixels);
                check(gallery_renderer.bundle_count() == 1 &&
                          gallery_renderer.draw_stats().instances > 0,
                      "Native animation duplicated effect selections or lost instances");
                if (stage == 0)
                    first = pixels;
                else
                    check(visible(pixels) > visible(first) &&
                              dominant(pixels, 1) > dominant(first, 1),
                          "Keyframed size/color failed to change native particle pixels");
            }
            check(live.document().at("asset_id") == source.at("asset_id"),
                  "Native observation changed scene identity");
            context->WaitForIdle();
        }
    if (!gallery.empty() &&
        std::filesystem::exists(gallery / "Scenes/62-coordinated-animation.scene.json")) {
        Json source;
        std::ifstream(gallery / "Scenes/62-coordinated-animation.scene.json") >> source;
        EngineContext engine(WorldRole::Runtime, false, {animation_module(gallery)});
        Scene live(engine.world());
        live.restore_snapshot(source);
        RuntimeSimulation simulation(engine.world(), live);
        const auto animation = animation_runtime(engine.world());
        const auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(gallery));
        animation->catalog(catalog);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Native Gallery62 sequence admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        simulation.reset_presentation();
        auto gallery_host = std::make_shared<MeshResourceHost>(presentation, context, gallery);
        gallery_host->catalog(catalog);
        MeshSceneRenderer gallery_renderer(gallery_host);
        active_host = gallery_host;
        active_renderer = &gallery_renderer;
        Target wide(device, 960, 540);
        std::vector<std::array<std::uint8_t, 4>> first;
        for (unsigned stage = 0; stage < 2; ++stage) {
            if (stage)
                for (unsigned tick = 0; tick < 10; ++tick)
                    simulation.tick(.25f);
            scene = extract_render_scene(simulation.presentation(1));
            check(scene.diagnostics.empty() && scene.cameras.size() == 1 &&
                      scene.meshes.size() == 5 && scene.lights.size() == 1,
                  "Native sequence lost actual camera/light/mesh participants");
            camera = scene.cameras.front().camera;
            camera_world = scene.cameras.front().world;
            const auto pixels = draw(wide);
            check(visible(pixels) > 1000, "Native sequence produced an empty frame");
            image(root / ("gallery62-phase" + std::to_string(stage) + ".ppm"), 960, 540, pixels);
            if (!stage)
                first = pixels;
            else {
                std::size_t changed = 0;
                for (std::size_t i = 0; i < first.size(); ++i)
                    changed += first[i] != pixels[i];
                check(changed > 10000,
                      "Shared native renderer did not consume coordinated sequence changes");
            }
        }
        context->WaitForIdle();
    }
    if (!gallery.empty() &&
        std::filesystem::exists(gallery / "Scenes/63-portable-prefab-rigs.scene.json")) {
        Json document;
        std::ifstream(gallery / "Scenes/63-portable-prefab-rigs.scene.json") >> document;
        const auto source = load_game_scene(gallery, {document.at("asset_id").get<AssetId>()});
        EngineContext engine(WorldRole::Runtime, false,
                             {physics_module({}, gallery), animation_module(gallery)});
        Scene live(engine.world());
        live.restore_snapshot(source);
        RuntimeSimulation simulation(engine.world(), live);
        const auto animation = animation_runtime(engine.world());
        const auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(gallery));
        animation->catalog(catalog);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!animation->prepare_initial_pose()) {
            check(std::chrono::steady_clock::now() < deadline,
                  "Native Gallery63 portable animation admission timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto physics = std::static_pointer_cast<PhysicsRuntime>(engine.services().physics());
        physics->synchronize(0);
        check(physics->status().at("joints") == 2, "Native portable constraints missing");
        simulation.reset_presentation();
        auto gallery_host = std::make_shared<MeshResourceHost>(presentation, context, gallery);
        gallery_host->catalog(catalog);
        MeshSceneRenderer gallery_renderer(gallery_host);
        active_host = gallery_host;
        active_renderer = &gallery_renderer;
        Target wide(device, 960, 540);
        std::vector<std::array<std::uint8_t, 4>> first;
        for (unsigned stage = 0; stage < 2; ++stage) {
            if (stage)
                for (unsigned tick = 0; tick < 120; ++tick)
                    simulation.tick(1.f / 60);
            scene = extract_render_scene(simulation.presentation(1));
            check(scene.diagnostics.empty() && scene.cameras.size() == 1 &&
                      scene.meshes.size() == 13 && scene.lights.size() == 1,
                  "Native portable Gallery lost render participants");
            camera = scene.cameras.front().camera;
            camera_world = scene.cameras.front().world;
            const auto pixels = draw(wide);
            check(visible(pixels) > 1000, "Native portable Gallery produced an empty frame");
            image(root / ("gallery63-phase" + std::to_string(stage) + ".ppm"), 960, 540, pixels);
            if (!stage)
                first = pixels;
            else {
                std::size_t changed = 0;
                for (std::size_t i = 0; i < first.size(); ++i)
                    changed += first[i] != pixels[i];
                check(changed > 1000, "Native portable Gallery motion did not reach the renderer");
            }
        }
        context->WaitForIdle();
    }
    if (!gallery.empty() &&
        std::filesystem::exists(gallery / "Scenes/64-runtime-prefab-creation.scene.json")) {
        const auto asset = AssetId::parse("fab9f548-3f22-4fbc-a5e0-bd0a2cffd9a1");
        const auto prefab = AssetId::parse("3f95b972-aa2a-4fab-999a-8857a5a35e11");
        EngineContext engine(WorldRole::Runtime);
        Scene live(engine.world());
        live.restore_snapshot(load_game_scene(gallery, {asset}));
        RuntimeSimulation simulation(engine.world(), live);
        simulation.reset_presentation();
        const auto catalog =
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(gallery));
        auto gallery_host = std::make_shared<MeshResourceHost>(presentation, context, gallery);
        gallery_host->catalog(catalog);
        MeshSceneRenderer gallery_renderer(gallery_host);
        active_host = gallery_host;
        active_renderer = &gallery_renderer;
        Target wide(device, 960, 540);
        std::vector<std::array<std::uint8_t, 4>> previous;
        for (unsigned stage = 0; stage < 3; ++stage) {
            if (stage) {
                const unsigned count = stage == 1 ? 2 : 1;
                for (unsigned i = 0; i < count; ++i) {
                    const auto token = detail::request_runtime_prefab(
                        engine.world(), "project.native_gallery", asset, prefab, "Visible spawn");
                    detail::publish_runtime_entities(engine.world());
                    const auto info = detail::inspect_runtime_entity(
                        engine.world(), "project.native_gallery", token);
                    check(info.state == detail::RuntimeEntityState::Ready,
                          "Native Gallery64 creation failed: " + info.diagnostic);
                    engine.world()
                        .world()
                        .entity(info.native_entity)
                        .set<LocalTranslation>({stage == 1 ? (i == 0 ? -4.0 : 4.0) : 0.0, 1.3,
                                                stage == 2 ? -3.0 : 0.0})
                        .set<LocalRotation>({0, .38268343f, 0, .92387953f});
                    detail::release_runtime_entity(engine.world(), "project.native_gallery", token);
                }
                simulation.tick(1.f / 60);
            }
            scene = extract_render_scene(simulation.presentation(1));
            const std::size_t expected = stage == 0 ? 1 : stage == 1 ? 5 : 7;
            check(scene.diagnostics.empty() && scene.meshes.size() == expected &&
                      scene.cameras.size() == 1 && scene.lights.size() == 1,
                  "Native Gallery64 lost created render participants");
            camera = scene.cameras.front().camera;
            camera_world = scene.cameras.front().world;
            const auto pixels = draw(wide);
            check(visible(pixels) > 1000, "Native Gallery64 produced an empty stage");
            image(root / ("gallery64-stage" + std::to_string(stage) + ".ppm"), 960, 540, pixels);
            if (stage) {
                std::size_t changed = 0;
                for (std::size_t i = 0; i < previous.size(); ++i)
                    changed += previous[i] != pixels[i];
                check(changed > 500, "Created Prefabs did not reach the shared native renderer");
            }
            previous = pixels;
        }
        context->WaitForIdle();
    }
    context->WaitForIdle();
    std::cout << "Shared MeshSceneRenderer particle Vulkan readback, color, instancing, "
                 "owner scope, visibility/layers, resize, selection guards, lifetime and emitter "
                 "bound passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3, "Need native particle scratch root and optional Gallery");
        execute(std::filesystem::absolute(argv[1]) / AssetId::generate().str(),
                argc == 3 ? std::filesystem::absolute(argv[2]) : std::filesystem::path{});
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
