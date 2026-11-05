#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "hdr_bloom_gpu_tests.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
using test_sprite_draw::check;
void exercise(DiligentPresentation& presentation, IDeviceContext* context,
              const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    auto* device = presentation.device();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
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
    const auto create = [&](const char* name, Double3 position, Double3 scale) {
        const auto id =
            authoring_command(
                authored, "entity.create",
                {{"kind", 0},
                 {"name", name},
                 {"position", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}}})
                .at("selected")
                .get<std::string>();
        authored.entity(id).set<LocalScale>(checked_local_scale(scale));
    };
    create("Current-view occluder", {0, 0, 2}, {6, 5, .4});
    create("Hidden rigid cube", {0, 0, 7}, {2, 2, 2});
    create("Visible foreground cube", {-2, -.7, -1}, {.7, .7, .7});
    auto scene = extract_render_scene(authored.effective_document());
    RenderCamera camera{EntityId::generate(), {}, {}};
    camera.world.m[11] = -8;
    scene.cameras.push_back(camera);
    unsigned comparisons = 0;
    for (unsigned mode = 0; mode < 4; ++mode) {
        auto reference_scene = scene, culled_scene = scene;
        reference_scene.settings.temporal_anti_aliasing.enabled = mode != 0;
        if (mode >= 2) {
            reference_scene.settings.ambient_occlusion.enabled = true;
            reference_scene.settings.screen_space_reflections.enabled = true;
            reference_scene.settings.motion_blur.enabled = true;
            reference_scene.settings.depth_of_field.enabled = true;
            reference_scene.settings.depth_of_field.focus_distance = 10;
        }
        culled_scene.settings = reference_scene.settings;
        culled_scene.settings.occlusion_culling.enabled = true;
        FrameRenderer reference(presentation), culled(presentation);
        reference.resources(host);
        culled.resources(host);
        const auto frame = [&](FrameRenderer& owner, const RenderScene& source, double seconds) {
            presentation.begin_frame();
            auto* output = owner.game(context, source, 193, 127, seconds);
            presentation.end_frame(context);
            host->submit();
            auto pixels = test_sprite_draw::read_pixels(device, context, output->GetTexture());
            context->FinishFrame();
            device->ReleaseStaleResources();
            if (!owner.diagnostics().empty())
                throw std::runtime_error(owner.diagnostics().front().text);
            return pixels;
        };
        // Settle both existing resource consumers, then reset only presentation
        // history so their current sample sequences start at the same point.
        for (unsigned warmup = 0; warmup < 8; ++warmup) {
            (void)frame(reference, reference_scene, warmup / 60.);
            (void)frame(culled, culled_scene, warmup / 60.);
        }
        check(!reference.pending() && !culled.pending(), "Occlusion view resources unresolved");
        reference.invalidate_temporal_history();
        culled.invalidate_temporal_history();
        for (unsigned sample = 0; sample < 8; ++sample) {
            const auto a = frame(reference, reference_scene, 1 + sample / 60.);
            const auto b = frame(culled, culled_scene, 1 + sample / 60.);
            check(a == b, "Culling changed Game/TAA/surface-effect reference pixels");
            ++comparisons;
        }
        if (mode)
            check(reference.temporal_ready() && culled.temporal_ready(),
                  "Culling interrupted temporal history admission");
        if (mode == 3) {
            const auto hdr = [&](FrameRenderer& owner, const RenderScene& source) {
                auto* image = owner.render(context, source, owner.cameras(), 193, 127, 0, nullptr,
                                           2, nullptr, FrameRenderer::Output::LinearHdr);
                auto pixels = test_bloom::read(device, context, image->GetTexture());
                check(owner.diagnostics().empty(), "Occlusion HDR diagnostics");
                return pixels;
            };
            check(hdr(reference, reference_scene) == hdr(culled, culled_scene),
                  "Culling changed upstream HDR reference output");
            ++comparisons;
        }
    }
    forge::Viewport preview(presentation, true);
    preview.resources(host);
    preview.preview_lighting(true);
    EditorCamera orbit;
    orbit.target = {0, 0, 2};
    orbit.yaw = orbit.pitch = 0;
    orbit.distance = 10;
    const auto preview_frame = [&] {
        presentation.begin_frame();
        auto* image = preview.render(context, authored.effective_document(), 193, 127, orbit,
                                     authored.revision(), false, {false});
        presentation.end_frame(context);
        host->submit();
        auto pixels = test_sprite_draw::read_pixels(device, context, image->GetTexture());
        context->FinishFrame();
        device->ReleaseStaleResources();
        check(preview.diagnostics().empty(), "Occlusion Scene View diagnostics");
        return pixels;
    };
    test_sprite_draw::Pixels baseline;
    for (unsigned warmup = 0; warmup < 8; ++warmup)
        baseline = preview_frame();
    authoring_command(authored, "scene.rendering.set",
                      {{"occlusion_culling", {{"enabled", true}}}});
    check(preview_frame() == baseline && preview.meshes()->draw_stats().indirect_calls > 0,
          "Scene View omitted culling or changed reference pixels");
    const auto retained = preview.retained;
    check(preview_frame() == baseline && preview.retained > retained,
          "Unchanged Scene View did not retain culling output");
    check(authored.undo() && preview_frame() == baseline &&
              preview.meshes()->draw_stats().indirect_calls == 0 && authored.redo() &&
              preview_frame() == baseline && preview.meshes()->draw_stats().indirect_calls > 0,
          "Scene culling Undo/Redo failed to invalidate the retained view");
    std::cout << "Occlusion Game/TAA/MRT/focus/motion/HDR exact pairs=" << comparisons
              << "; actual Scene View/retention/Undo/Redo passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected isolated view fixture directory");
        EngineVkCreateInfo create;
        create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Occlusion views device unavailable");
        DiligentPresentation presentation(device);
        exercise(presentation, context, argv[1]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
