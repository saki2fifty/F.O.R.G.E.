#pragma once
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/transform.hpp>

namespace forge::test_fast_aa_scene {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& project) {
    using test_sprite_draw::check;
    using test_sprite_draw::Pixels;
    std::filesystem::create_directories(project);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(project));
    MaterialResourceData material;
    material.values.model = "forge.gltf.unlit.v1";
    material.values.double_sided = true;
    material.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    material.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(engine_material(), material);
    EngineContext engine;
    Scene authored(engine.world());
    authoring_command(authored, "scene.rendering.set",
                      {{"shadows", {{"enabled", false}}}, {"environment", {{"sky", false}}}});
    const std::string id = authoring_command(authored, "entity.create",
                                             {{"kind", 0},
                                              {"name", "Tilted checker cube"},
                                              {"position", {{"x", 0}, {"y", 0}, {"z", 0}}}})
                               .at("selected");
    authored.entity(id).set<LocalRotation>(rotation_from_euler({12, 28, 17}));
    auto scene = extract_render_scene(authored.effective_document());
    RenderCamera camera{EntityId::generate(), {}, {}};
    camera.world.m[11] = -4;
    scene.cameras.push_back(camera);
    FrameRenderer game(presentation);
    game.resources(host);
    double seconds = 0;
    const auto game_frame = [&](const RenderScene& value) {
        Pixels result;
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            presentation.begin_frame();
            auto* image = game.game(context, value, 122, 81, seconds);
            seconds += 1. / 60;
            presentation.end_frame(context);
            host->submit();
            result =
                test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            if (!game.pending())
                break;
        }
        check(!game.pending() && game.diagnostics().empty(),
              "Spatial AA Game resources did not resolve");
        return result;
    };
    const auto baseline = game_frame(scene);
    scene.settings.fast_approximate_anti_aliasing.enabled = true;
    const auto filtered = game_frame(scene);
    check(filtered != baseline, "FrameRenderer did not apply authored spatial AA");
    scene.settings.fast_approximate_anti_aliasing.enabled = false;
    check(game_frame(scene) == baseline, "Game disabling spatial AA changed ordinary pixels");
    scene.settings.temporal_anti_aliasing.enabled = true;
    scene.settings.fast_approximate_anti_aliasing.enabled = true;
    for (unsigned sample = 0; sample < 8; ++sample)
        (void)game_frame(scene);
    check(game.temporal_ready(), "Spatial AA prevented actual temporal AA history admission");
    scene.settings.temporal_anti_aliasing.enabled = false;
    (void)game_frame(scene);
    const auto hdr_frame = [&](bool enabled) {
        scene.settings.fast_approximate_anti_aliasing.enabled = enabled;
        presentation.begin_frame();
        auto* image = game.render(context, scene, game.cameras(), 122, 81, 0, nullptr, seconds,
                                  nullptr, FrameRenderer::Output::LinearHdr);
        presentation.end_frame(context);
        host->submit();
        return test_bloom::read(presentation.device(), context, image->GetTexture());
    };
    const auto hdr = hdr_frame(false);
    check(hdr_frame(true) == hdr, "Display AA modified upstream linear HDR scene output");

    Viewport preview(presentation, true);
    preview.resources(host);
    preview.preview_lighting(true);
    EditorCamera orbit;
    orbit.target = {0, 0, 0};
    orbit.yaw = orbit.pitch = 0;
    orbit.distance = 4;
    const auto preview_frame = [&] {
        presentation.begin_frame();
        auto* image = preview.render(context, authored.effective_document(), 122, 81, orbit,
                                     authored.revision(), false, {false});
        presentation.end_frame(context);
        host->submit();
        auto pixels =
            test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        return pixels;
    };
    Pixels scene_baseline;
    for (unsigned attempt = 0; attempt < 8; ++attempt)
        scene_baseline = preview_frame();
    check(!preview.meshes()->pending() && preview.meshes()->diagnostics().empty(),
          "Spatial AA Scene View resources did not resolve");
    authoring_command(authored, "scene.rendering.set",
                      {{"fast_approximate_anti_aliasing", {{"enabled", true}}}});
    const auto scene_filtered = preview_frame();
    check(scene_filtered != scene_baseline, "Scene View omitted authored spatial AA");
    const auto retained = preview.retained;
    check(preview_frame() == scene_filtered && preview.retained > retained,
          "Unchanged Scene View did not retain its filtered output");
    check(authored.undo() && preview_frame() == scene_baseline && authored.redo() &&
              preview_frame() == scene_filtered,
          "Scene AA Undo/Redo did not invalidate retained display output");
}
} // namespace forge::test_fast_aa_scene
