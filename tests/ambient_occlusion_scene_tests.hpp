#pragma once
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <thread>

namespace forge::test_occlusion_scene {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& project) {
    using test_sprite_draw::check;
    using test_sprite_draw::Pixels;
    std::filesystem::create_directories(project);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(project));
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    scene.settings.environment.texture =
        engine_texture(EngineTexture::White, TextureDimension::Cube);
    scene.settings.environment.sky = false;
    scene.settings.environment.intensity = .6f;
    scene.settings.ambient_occlusion = {true, .7f, 1, .01f};
    RenderCamera camera{EntityId::generate(), {}, {}};
    camera.world.m[11] = -4;
    camera.camera.projection = unsigned(CameraProjection::Orthographic);
    camera.camera.orthographic_height = 3;
    scene.cameras.push_back(camera);
    for (unsigned i = 0; i < 2; ++i) {
        RenderMesh slab{EntityId::generate(), {}, {}};
        slab.renderer.mesh = engine_primitive(0);
        slab.world.m[0] = i ? 2 : 4;
        slab.world.m[5] = 2.8;
        slab.world.m[10] = .1;
        slab.world.m[3] = i ? 1 : 0;
        slab.world.m[11] = i ? 0 : .3;
        scene.meshes.push_back(slab);
    }
    Light light;
    light.intensity = 2;
    scene.lights.push_back({EntityId::generate(), light_view(light, {})});
    FrameRenderer frame(presentation), reference(presentation);
    frame.resources(host);
    reference.resources(host);
    double time = 1;
    const auto render = [&](FrameRenderer& owner, const RenderScene& source, unsigned width = 194,
                            unsigned height = 129, bool warning = false,
                            const TemporalFrameSample* sample = nullptr) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* output = owner.game(context, source, width, height, time, sample);
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, output->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            time += 1. / 60;
            check(std::chrono::steady_clock::now() < deadline, "Ambient Scene resources stalled");
        } while (owner.pending());
        if (!warning && !owner.diagnostics().empty())
            throw std::runtime_error("Ambient Scene: " + owner.diagnostics().front().text);
        return pixels;
    };
    auto sharp = scene;
    sharp.settings.ambient_occlusion.enabled = false;
    const auto baseline = render(reference, sharp), occluded = render(frame, scene);
    std::size_t changes = 0;
    for (std::size_t i = 0; i < baseline.size(); ++i) {
        changes += baseline[i] != occluded[i];
        check(baseline[i][3] == occluded[i][3], "Ambient Game changed current alpha");
        for (unsigned c = 0; c < 3; ++c)
            check(occluded[i][c] <= baseline[i][c], "Ambient Game added light");
    }
    check(changes > 100, "Actual Game ambient occlusion has no depth-step signal");
    check(render(frame, scene) == occluded, "Spatial-only ambient Game changed a static image");
    auto detailed = scene;
    detailed.settings.ambient_occlusion.normal_influence = 1;
    const auto normal_occluded = render(frame, detailed);
    std::size_t normal_changes = 0;
    for (std::size_t pixel = 0; pixel < baseline.size(); ++pixel) {
        normal_changes += normal_occluded[pixel] != baseline[pixel];
        check(normal_occluded[pixel][3] == baseline[pixel][3],
              "Normal-only Game data changed current alpha");
    }
    check(normal_changes > 100 && render(frame, detailed) == normal_occluded,
          "Normal-only actual Game has no stable occlusion signal");
    for (unsigned zero = 0; zero < 3; ++zero) {
        auto skipped = detailed;
        if (zero == 0)
            skipped.settings.ambient_occlusion.enabled = false;
        if (zero == 1)
            skipped.settings.ambient_occlusion.radius = 0;
        if (zero == 2)
            skipped.settings.ambient_occlusion.strength = 0;
        check(render(frame, skipped) == baseline, "Ambient zero path changed ordinary Game pixels");
    }
    auto direct = detailed;
    direct.settings.environment.intensity = 0;
    auto direct_sharp = direct;
    direct_sharp.settings.ambient_occlusion.enabled = false;
    check(render(frame, direct) == render(reference, direct_sharp),
          "Ambient Game darkened direct lighting without an environment contribution");
    auto load = detailed;
    load.cameras[0].camera.clear_depth = false;
    const auto fallback = render(frame, load, 194, 129, true);
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) {
                          return d.category == "render.temporal.unavailable" &&
                                 d.text.find("clear") != std::string::npos;
                      }),
          "Ambient camera-load fallback did not explain the missing depth clear");
    load.settings.ambient_occlusion.enabled = false;
    check(fallback == render(reference, load),
          "Ambient rejected camera changed ordinary rendering");
    auto split = detailed;
    split.cameras[0].camera.viewport_width = .5;
    auto second = split.cameras[0];
    second.entity = EntityId::generate();
    second.camera.viewport_x = .5;
    second.camera.orthographic_height = 2;
    split.cameras.push_back(second);
    const auto joined = render(frame, split, 194, 129);
    auto left = scene, right = scene;
    right.cameras[0] = second;
    right.cameras[0].camera.viewport_x = 0;
    right.cameras[0].camera.viewport_width = 1;
    const auto a = render(reference, left, 97, 129), b = render(reference, right, 97, 129);
    for (unsigned y = 0; y < 129; ++y)
        for (unsigned x = 0; x < 97; ++x) {
            check(joined[y * 194 + x] == a[y * 97 + x],
                  "Ambient left camera crossed its rectangle");
            check(joined[y * 194 + 97 + x] == b[y * 97 + x],
                  "Ambient right camera crossed its rectangle");
        }
    auto combined = detailed;
    combined.settings.temporal_anti_aliasing = {true, .8f};
    combined.settings.motion_blur = {true, .5f, 16};
    combined.settings.depth_of_field = {true, 4, 1, 4};
    const TemporalFrameSample sample{AssetId::generate(), .25};
    for (unsigned i = 0; i < 8; ++i)
        (void)render(frame, combined, 194, 129, false, &sample);
    check(frame.temporal_ready(), "Ambient composition blocked combined explicit-sample readiness");
    EngineContext engine;
    Scene authored(engine.world());
    authoring_command(authored, "entity.create", {{"kind", 0}});
    authoring_command(
        authored, "scene.rendering.set",
        {{"environment", {{"texture", scene.settings.environment.texture.id}, {"sky", false}}},
         {"ambient_occlusion", {{"enabled", true}, {"normal_influence", .7}}}});
    const auto document = authored.preview_document(authored.document());
    Viewport preview(presentation, true);
    preview.resources(host);
    preview.preview_lighting(true);
    EditorCamera orbit;
    orbit.distance = 5;
    orbit.yaw = .4f;
    orbit.pitch = .2f;
    for (unsigned i = 0; i < 20; ++i) {
        presentation.begin_frame();
        auto* output = preview.render(context, document, 97, 81, orbit, 1, false, {false});
        presentation.end_frame(context);
        host->submit();
        (void)test_sprite_draw::read_pixels(presentation.device(), context, output->GetTexture());
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    }
    check(preview.retained > 0 && preview.meshes()->diagnostics().empty(),
          "Spatial-only ambient Scene failed to settle into retained rendering");
    context->WaitForIdle();
    std::cout << "Actual ambient Game and retained Scene PASS: " << changes
              << " occluded pixels, zero/direct/alpha, camera isolation/load fallback and combined "
                 "capture\n";
}
} // namespace forge::test_occlusion_scene
