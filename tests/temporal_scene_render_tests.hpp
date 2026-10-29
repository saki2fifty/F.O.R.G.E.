#pragma once
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include <forge/animation.hpp>
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <thread>

namespace forge::test_temporal_scene {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& project) {
    using test_sprite_draw::check;
    using test_sprite_draw::Pixels;
    auto* device = presentation.device();
    std::filesystem::create_directories(project);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(project));
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    scene.settings.environment.sky = false;
    scene.settings.temporal_anti_aliasing = {true, .85f};
    RenderCamera camera{EntityId::generate(), {}, {}};
    camera.world.m[11] = -4;
    scene.cameras.push_back(camera);
    RenderMesh cube{EntityId::generate(), {}, {}};
    cube.renderer.mesh = engine_primitive(0);
    cube.legacy_tint = std::array<float, 3>{.9f, .4f, .1f};
    scene.meshes.push_back(cube);
    Light light;
    light.intensity = 4;
    scene.lights.push_back({EntityId::generate(), light_view(light, {})});
    FrameRenderer frame(presentation);
    frame.resources(host);
    double time = 1;
    const auto render = [&](FrameRenderer& owner, const RenderScene& source, unsigned w = 97,
                            unsigned h = 81, bool allow_diagnostic = false,
                            const TemporalFrameSample* sample = nullptr) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* output = owner.game(context, source, w, h, time, sample);
            presentation.end_frame(context);
            host->submit();
            pixels = test_sprite_draw::read_pixels(device, context, output->GetTexture());
            context->FinishFrame();
            device->ReleaseStaleResources();
            time += 1. / 60;
            check(std::chrono::steady_clock::now() < deadline, "Temporal Scene resources stalled");
        } while (owner.pending());
        if (!allow_diagnostic && !owner.diagnostics().empty())
            throw std::runtime_error("Temporal Scene: " + owner.diagnostics().front().text);
        return pixels;
    };
    const auto fresh = [&](const RenderScene& source, unsigned w = 97, unsigned h = 81) {
        FrameRenderer reference(presentation);
        reference.resources(host);
        return render(reference, source, w, h);
    };
    auto baseline = render(frame, scene);
    check(std::count_if(baseline.begin(), baseline.end(),
                        [&](const auto& p) { return p != baseline.front(); }) > 100,
          "Temporal Scene fixture has no visible Mesh geometry");
    for (unsigned i = 0; i < 12; ++i)
        baseline = render(frame, scene);
    {
        FrameRenderer raw(presentation), accumulated(presentation);
        raw.resources(host);
        accumulated.resources(host);
        auto edges = scene;
        const double angle = .31;
        edges.meshes[0].world.m[0] = edges.meshes[0].world.m[5] = std::cos(angle);
        edges.meshes[0].world.m[1] = -std::sin(angle);
        edges.meshes[0].world.m[4] = std::sin(angle);
        auto current_only = edges;
        current_only.settings.temporal_anti_aliasing.stability = 0;
        Pixels last_raw, last_accumulated;
        double raw_variation = 0, accumulated_variation = 0;
        for (unsigned i = 0; i < 16; ++i) {
            const auto a = render(raw, current_only), b = render(accumulated, edges);
            if (i >= 8)
                for (std::size_t p = 0; p < a.size(); ++p)
                    for (unsigned channel = 0; channel < 3; ++channel) {
                        const double da = int(a[p][channel]) - int(last_raw[p][channel]);
                        const double db = int(b[p][channel]) - int(last_accumulated[p][channel]);
                        raw_variation += da * da;
                        accumulated_variation += db * db;
                    }
            last_raw = a;
            last_accumulated = b;
        }
        std::cout << "Actual Scene stationary edge variation: current=" << raw_variation
                  << ", accumulated=" << accumulated_variation << '\n';
        check(raw_variation > 1000 && accumulated_variation < raw_variation * .8,
              "Shared Scene temporal filter did not reduce stationary edge variation");
    }
    {
        const TemporalFrameSample sample{AssetId::generate(), .25};
        const auto first_sample = render(frame, scene, 97, 81, false, &sample);
        check(!frame.temporal_ready(),
              "One explicit raster frame incorrectly reported warm history");
        for (unsigned i = 1; i < 8; ++i) {
            time += 10; // Explicit frozen samples must not depend on wall-clock/GPU delay.
            (void)render(frame, scene, 97, 81, false, &sample);
        }
        check(frame.temporal_ready(), "One jitter cycle did not finish explicit sample history");
        auto next = sample;
        next.seconds = .5;
        check(render(frame, scene, 97, 81, false, &next) == first_sample && !frame.temporal_ready(),
              "New explicit sample retained history from an earlier sample");
        next.operation = AssetId::generate();
        check(render(frame, scene, 97, 81, false, &next) == first_sample,
              "New capture operation reused an older sample's history");
        check(render(frame, scene) == first_sample,
              "Leaving explicit capture retained sample accumulation");
    }
    // A rewind and long interruption must be byte-identical to the actual fresh raster phase.
    time = .1;
    check(render(frame, scene) == fresh(scene), "Time rewind retained old temporal pixels");
    time += 10;
    check(render(frame, scene) == fresh(scene), "Long interruption retained temporal pixels");
    check(render(frame, scene, 63, 57) == fresh(scene, 63, 57),
          "Temporal resize did not start a fresh view");
    auto cut = scene;
    cut.cameras[0].world.m[3] = 20;
    check(render(frame, cut) == fresh(cut), "Camera cut retained previous temporal pixels");
    auto projected = scene;
    projected.cameras[0].camera.projection = unsigned(CameraProjection::Orthographic);
    projected.cameras[0].camera.orthographic_height = 3;
    check(render(frame, projected) == fresh(projected),
          "Projection change retained temporal pixels");
    auto replaced = scene;
    replaced.scene = AssetId::generate();
    check(render(frame, replaced) == fresh(replaced), "Scene replacement retained temporal pixels");
    frame.resources(host);
    check(render(frame, scene) == fresh(scene), "Provider replacement retained temporal pixels");
    auto split = scene;
    split.cameras[0].camera.viewport_width = .5;
    auto right = split.cameras[0];
    right.entity = EntityId::generate();
    right.camera.viewport_x = .5;
    right.camera.background_r = .4f;
    right.world.m[3] = .3;
    split.cameras.push_back(right);
    const auto first_split = render(frame, split);
    check(first_split == fresh(split), "Split camera targets mixed their first-frame history");
    for (unsigned i = 0; i < 10; ++i)
        (void)render(frame, split);
    {
        auto rectangles = split;
        rectangles.meshes.clear();
        rectangles.cameras[0].camera.background_a = .375f;
        rectangles.cameras[1].camera.background_a = .625f;
        rectangles.cameras[0].camera.viewport_x = .1;
        rectangles.cameras[0].camera.viewport_y = .2;
        rectangles.cameras[0].camera.viewport_width = .6;
        rectangles.cameras[0].camera.viewport_height = .6;
        rectangles.cameras[1].camera.order = 1;
        rectangles.cameras[1].camera.viewport_x = .4;
        rectangles.cameras[1].camera.viewport_y = .3;
        rectangles.cameras[1].camera.viewport_width = .5;
        rectangles.cameras[1].camera.viewport_height = .5;
        const auto temporal = render(frame, rectangles);
        rectangles.settings.temporal_anti_aliasing.enabled = false;
        check(temporal == render(frame, rectangles),
              "Temporal rectangle composition changed overlap or pixels outside camera views");
    }
    // Actual authored and camera motion traverse shared queue/history state.
    for (unsigned i = 0; i < 8; ++i) {
        split.meshes[0].world.m[3] = .02 * i;
        split.cameras[0].world.m[3] = .01 * i;
        (void)render(frame, split);
    }
    auto overlay = scene;
    overlay.meshes.clear();
    auto second = overlay.cameras[0];
    second.entity = EntityId::generate();
    second.camera.order = 1;
    second.camera.clear_color = false;
    second.camera.clear_depth = false;
    second.camera.viewport_x = .25;
    second.camera.viewport_width = .5;
    overlay.cameras.push_back(second);
    const auto fallback = render(frame, overlay, 97, 81, true);
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) {
                          return d.category == "render.temporal.unavailable" &&
                                 d.severity == Severity::Warning;
                      }),
          "Loaded camera did not explain temporal fallback");
    overlay.settings.temporal_anti_aliasing.enabled = false;
    check(fallback == render(frame, overlay), "Temporal fallback changed camera load composition");
    auto disabled = scene;
    disabled.settings.temporal_anti_aliasing.enabled = false;
    check(render(frame, disabled) == fresh(disabled),
          "Disabled temporal path retained accumulation");
    {
        const AssetRef<MaterialAsset> material{AssetId::generate()};
        MaterialResourceData surface;
        surface.values.model = "forge.gltf.unlit.v1";
        surface.values.double_sided = true;
        surface.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
        surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
        host->preview_material(material, surface);
        auto layered = scene;
        RenderSprite sprite;
        sprite.entity = EntityId::generate();
        sprite.renderer.material = material;
        sprite.renderer.order = -1; // The shared opaque queue intentionally puts this first.
        sprite.renderer.width = sprite.renderer.height = 2;
        sprite.world.m[11] = -.5;
        layered.sprites.push_back(sprite);
        const auto retained_order = render(frame, layered, 97, 81, true);
        check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                          [](const auto& d) {
                              return d.category == "render.temporal.unavailable" &&
                                     d.severity == Severity::Warning &&
                                     d.text.find("ordinary draw ordering") != std::string::npos;
                          }),
              "Interleaved opaque layers did not explain whole-view temporal fallback");
        layered.settings.temporal_anti_aliasing.enabled = false;
        check(retained_order == render(frame, layered),
              "Temporal profile changed the existing opaque Sprite/Mesh ordering");
    }
    {
        MeshSceneRenderer bounded(host, Diligent::TEX_FORMAT_RGBA16_FLOAT, mesh_pose_payload_limit,
                                  1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        do {
            bounded.update(scene);
            check(std::chrono::steady_clock::now() < deadline,
                  "Bounded temporal draw admission stalled");
        } while (bounded.pending());
        auto color = ::texture(device, 97, 81, Diligent::TEX_FORMAT_RGBA16_FLOAT,
                               Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE);
        auto depth =
            ::texture(device, 97, 81, Diligent::TEX_FORMAT_D32_FLOAT, Diligent::BIND_DEPTH_STENCIL);
        auto* rtv = color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
        const auto view = prepare_game_cameras(scene, 97, 81).cameras.at(0).view;
        bounded.shadows(scene, view, UINT32_MAX);
        context->SetRenderTargets(1, &rtv, dsv,
                                  Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[4]{};
        context->ClearRenderTarget(rtv, black, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1, 0,
                                   Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport area{0, 0, 97, 81, 0, 1};
        context->SetViewports(1, &area, 97, 81);
        MeshSceneRenderer::TemporalPass pass;
        TemporalMeshParts previous, current;
        pass.current = pass.previous = view;
        pass.previous_parts = &previous;
        pass.current_parts = &current;
        bool switched_targets = false;
        pass.begin = [&] { switched_targets = true; };
        pass.resolve = [](bool) {};
        bounded.draw(scene, view, UINT32_MAX, color, dsv, &pass);
        check(!switched_targets && !pass.applied && bounded.draw_stats().calls == 1 &&
                  std::any_of(bounded.diagnostics().begin(), bounded.diagnostics().end(),
                              [](const auto& d) {
                                  return d.category == "render.temporal.unavailable" &&
                                         d.severity == Severity::Warning;
                              }),
              "Temporal specialization exceeded capacity or hid the ordinary valid draw");
    }
    {
        EngineContext engine;
        Scene authored(engine.world());
        const auto created = authoring_command(
            authored, "entity.create", {{"kind", 0}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}}});
        const std::string entity = created.at("selected");
        authoring_command(authored, "scene.rendering.set",
                          {{"temporal_anti_aliasing", {{"enabled", true}}}});
        auto document = authored.effective_document();
        Viewport preview(presentation, true);
        preview.resources(host);
        preview.preview_lighting(true);
        EditorCamera orbit;
        orbit.target = {0, 0, 0};
        orbit.yaw = .4f;
        orbit.pitch = .2f;
        orbit.distance = 4;
        const auto preview_frame = [&] {
            presentation.begin_frame();
            auto* output = preview.render(context, document, 97, 81, orbit, 1, false, {false});
            presentation.end_frame(context);
            host->submit();
            const auto pixels =
                test_sprite_draw::read_pixels(device, context, output->GetTexture());
            context->FinishFrame();
            device->ReleaseStaleResources();
            return pixels;
        };
        for (unsigned i = 0; i < 32; ++i)
            (void)preview_frame();
        check(!preview.meshes()->pending() && preview.retained > 0 &&
                  preview.meshes()->diagnostics().empty(),
              "Temporal Scene viewport did not settle or finish resource admission");
        const auto draws = preview.redraws;
        const auto settled = preview_frame();
        check(preview_frame() == settled && preview.redraws == draws,
              "Settled temporal Scene viewport continued rendering");
        check(preview.pick(document, orbit, 97, 81, 48.5, 40.5) == entity,
              "Temporal projection jitter changed Scene picking");
        orbit.yaw += .15f;
        for (unsigned i = 0; i < 12; ++i)
            (void)preview_frame();
        check(preview.redraws >= draws + 8 && preview.redraws <= draws + 9,
              "A changed editor camera did not settle over one bounded jitter cycle");
        const auto after_motion = preview.redraws;
        (void)preview_frame();
        check(preview.redraws == after_motion,
              "Changed editor camera kept rendering after temporal settling");
    }
    context->WaitForIdle();
    std::cout << "Actual shared temporal Scene, completed frames, motion, projection, resize, "
                 "cuts, split cameras, load fallback and disabled pixels PASS\n";
}
} // namespace forge::test_temporal_scene

namespace forge::test_temporal_scene {
inline void gallery(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& project, const std::filesystem::path& images) {
    using test_sprite_draw::check;
    const auto id = AssetId::parse("4df8a08b-a5e1-407b-8321-5f80efa720e2");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    EngineContext engine(WorldRole::Runtime, false, {animation_module(project)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(project, {id}));
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!animation->prepare_initial_pose()) {
        check(std::chrono::steady_clock::now() < deadline, "Temporal Gallery animation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    FrameRenderer renderer(presentation);
    renderer.resources(host);
    std::filesystem::create_directories(images);
    double time = 0;
    for (const auto& [w, h] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        test_sprite_draw::Pixels first;
        for (unsigned sample = 0; sample < 2; ++sample) {
            if (sample)
                for (unsigned i = 0; i < 90; ++i)
                    simulation.tick(1.f / 60);
            const auto snapshot = extract_render_scene(simulation.presentation(1));
            Diligent::ITextureView* output = nullptr;
            unsigned warmup = 0;
            do {
                presentation.begin_frame();
                output = renderer.game(context, snapshot, w, h, time);
                presentation.end_frame(context);
                host->submit();
                context->Flush();
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                time += 1. / 60;
                check(++warmup < 128, "Temporal Gallery view did not finish warmup");
            } while (renderer.pending() || !renderer.temporal_ready());
            if (!renderer.diagnostics().empty())
                throw std::runtime_error("Temporal Gallery: " + renderer.diagnostics()[0].text);
            const auto pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, output->GetTexture());
            if (sample == 0)
                first = pixels;
            else
                check(pixels != first, "Temporal Gallery animation did not change actual pixels");
            const auto path = images / ("temporal-gallery-" + std::to_string(w) + "x" +
                                        std::to_string(h) + "-" + std::to_string(sample) + ".ppm");
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << w << ' ' << h << "\n255\n";
            for (const auto& pixel : pixels)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Temporal Gallery image write failed");
        }
    }
    context->WaitForIdle();
    std::cout << "Gallery102 actual shared Game/animation/wide/tall temporal images PASS\n";
}
} // namespace forge::test_temporal_scene
