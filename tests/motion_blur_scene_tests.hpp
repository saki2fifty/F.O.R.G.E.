#pragma once
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <numeric>

namespace forge::test_motion_scene {
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
    scene.settings.environment.sky = false;
    scene.settings.motion_blur = {true, 1, 32};
    RenderCamera camera{EntityId::generate(), {}, {}};
    camera.world.m[11] = -4;
    camera.camera.projection = unsigned(CameraProjection::Orthographic);
    camera.camera.orthographic_height = 3;
    scene.cameras.push_back(camera);
    for (unsigned x = 0; x < 24; ++x) {
        RenderMesh cell{EntityId::generate(), {}, {}};
        cell.renderer.mesh = engine_primitive(0);
        cell.world.m[0] = .19;
        cell.world.m[5] = 1.5;
        cell.world.m[10] = .1;
        cell.world.m[3] = (double(x) - 11.5) * .19;
        cell.legacy_tint =
            x % 2 ? std::array<float, 3>{.9f, .4f, .1f} : std::array<float, 3>{.05f, .1f, .8f};
        scene.meshes.push_back(cell);
    }
    Light light;
    light.intensity = 4;
    scene.lights.push_back({EntityId::generate(), light_view(light, {})});
    double time = 1;
    const auto render = [&](FrameRenderer& owner, const RenderScene& source,
                            const TemporalFrameSample* sample = nullptr) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* output = owner.game(context, source, 129, 97, time, sample);
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, output->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            time += 1. / 60;
            check(std::chrono::steady_clock::now() < deadline,
                  "Motion Scene resource admission stalled");
        } while (owner.pending());
        if (!owner.diagnostics().empty())
            throw std::runtime_error("Motion Scene: " + owner.diagnostics()[0].text);
        return pixels;
    };
    const auto changes = [](const Pixels& a, const Pixels& b) {
        return std::inner_product(a.begin(), a.end(), b.begin(), std::size_t(0), std::plus<>(),
                                  [](const auto& x, const auto& y) { return std::size_t(x != y); });
    };
    for (bool aa : {false, true}) {
        auto source = scene;
        source.settings.temporal_anti_aliasing = {aa, .8f};
        auto sharp = source;
        sharp.settings.motion_blur.enabled = false;
        FrameRenderer blurred(presentation), current(presentation);
        blurred.resources(host);
        current.resources(host);
        for (unsigned i = 0; i < 8; ++i) {
            (void)render(blurred, source);
            (void)render(current, sharp);
        }
        auto moved = source;
        for (auto& cell : moved.meshes)
            cell.world.m[3] += .38;
        auto moved_sharp = moved;
        moved_sharp.settings.motion_blur.enabled = false;
        const auto active = render(blurred, moved), reference = render(current, moved_sharp);
        check(changes(active, reference) > 100,
              "Actual completed Mesh movement did not blur checker detail");
        if (!aa) {
            check(blurred.temporal_ready(), "Blur-only view required phantom AA warmup");
            check(render(blurred, moved) == render(current, moved_sharp),
                  "Stationary blur-only view did not settle sharp");
        }
        FrameRenderer captured(presentation), captured_sharp(presentation);
        captured.resources(host);
        captured_sharp.resources(host);
        TemporalFrameSample sample{AssetId::generate(), .25};
        for (unsigned i = 0; i < 8; ++i) {
            (void)render(captured, source, &sample);
            (void)render(captured_sharp, sharp, &sample);
        }
        sample.seconds = .5;
        Pixels frozen, unblurred;
        for (unsigned i = 0; i < 8; ++i) {
            frozen = render(captured, moved, &sample);
            unblurred = render(captured_sharp, moved_sharp, &sample);
        }
        check(captured.temporal_ready() && changes(frozen, unblurred) > 100,
              "Frozen sample lost actual motion during repeated AA warmup");
        sample.seconds = .1;
        check(render(captured, moved, &sample) == render(captured_sharp, moved_sharp, &sample),
              "Backward sample reused stale motion");
        sample.operation = AssetId::generate();
        sample.seconds = .5;
        check(render(captured, moved, &sample) == render(captured_sharp, moved_sharp, &sample),
              "New capture operation reused another operation's movement");
        check(render(captured, moved) == render(captured_sharp, moved_sharp),
              "Leaving capture retained a frozen trail");
        if (!aa) {
            (void)render(blurred, moved); // Capture checks above advanced presentation time.
            auto panned = moved;
            panned.cameras[0].world.m[3] += .38;
            auto panned_sharp = panned;
            panned_sharp.settings.motion_blur.enabled = false;
            check(changes(render(blurred, panned), render(current, panned_sharp)) > 100,
                  "Actual completed camera motion did not drive blur");
            blurred.invalidate_temporal_history();
            check(render(blurred, panned) == render(current, panned_sharp),
                  "Manual preview discontinuity retained a completed trail");
            auto split = source;
            split.cameras[0].camera.viewport_width = .5;
            auto second = split.cameras[0];
            second.entity = EntityId::generate();
            second.camera.viewport_x = .5;
            second.world.m[3] = .2;
            split.cameras.push_back(second);
            (void)render(blurred, split);
            for (auto& mesh : split.meshes)
                mesh.world.m[3] += .38;
            auto split_sharp = split;
            split_sharp.settings.motion_blur.enabled = false;
            check(changes(render(blurred, split), render(current, split_sharp)) > 100,
                  "Independent camera rectangles lost actual motion");
            blurred.resources(host);
            check(render(blurred, split) == render(current, split_sharp),
                  "Provider replacement retained old motion");
        }
    }
    {
        EngineContext engine;
        Scene authored(engine.world());
        for (unsigned x = 0; x < 12; ++x)
            authoring_command(
                authored, "entity.create",
                {{"kind", 0}, {"position", {{"x", (double(x) - 5.5) * .2}, {"y", 0}, {"z", 0}}}});
        authoring_command(authored, "scene.rendering.set", {{"motion_blur", {{"enabled", true}}}});
        const auto document = authored.preview_document(authored.document());
        Viewport preview(presentation, true);
        preview.resources(host);
        preview.preview_lighting(true);
        EditorCamera orbit;
        orbit.distance = 5;
        orbit.yaw = .4f;
        orbit.pitch = .2f;
        const auto draw = [&](Viewport& owner) {
            presentation.begin_frame();
            auto* image = owner.render(context, document, 129, 97, orbit, 1, false, {false});
            presentation.end_frame(context);
            host->submit();
            auto pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            return pixels;
        };
        for (unsigned i = 0; i < 20; ++i)
            (void)draw(preview);
        const auto before = preview.redraws;
        orbit.yaw += .08f;
        (void)draw(preview);
        const auto settled = draw(preview);
        check(preview.redraws == before + 2 && draw(preview) == settled &&
                  preview.redraws == before + 2,
              "Blur-only Scene navigation froze its moving image or kept redrawing");
        Viewport fresh(presentation, true);
        fresh.resources(host);
        fresh.preview_lighting(true);
        check(draw(fresh) == settled,
              "Retained Scene image did not return to a sharp current raster");
    }
    context->WaitForIdle();
    std::cout << "Actual shared Mesh motion blur, AA on/off, stationary settling and frozen "
                 "capture warmup/reset PASS\n";
}
} // namespace forge::test_motion_scene
