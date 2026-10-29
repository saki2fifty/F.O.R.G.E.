#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <iostream>
using namespace Diligent;
using forge::test_sprite_draw::check;
namespace {
void exercise(forge::DiligentPresentation& presentation, IDeviceContext* context,
              const std::filesystem::path& project, const std::filesystem::path& images,
              const forge::AssetId& id) {
    using namespace forge;
    std::filesystem::create_directories(images);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    EngineContext engine(WorldRole::Runtime, false);
    Scene authored(engine.world());
    authored.restore_snapshot(load_game_scene(project, {id}));
    RuntimeSimulation simulation(engine.world(), authored);
    simulation.reset_presentation();
    const auto scene = extract_render_scene(simulation.presentation(1));
    FrameRenderer frame(presentation), ordinary(presentation);
    frame.resources(host);
    ordinary.resources(host);
    double seconds = 1;
    const auto render = [&](FrameRenderer& owner, const RenderScene& source, unsigned width = 320,
                            unsigned height = 180, bool warning = false,
                            const TemporalFrameSample* sample = nullptr) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        test_sprite_draw::Pixels pixels;
        do {
            presentation.begin_frame();
            auto* target = owner.game(context, source, width, height, seconds, sample);
            presentation.end_frame(context);
            host->submit();
            pixels =
                test_sprite_draw::read_pixels(presentation.device(), context, target->GetTexture());
            context->FinishFrame();
            presentation.device()->ReleaseStaleResources();
            seconds += 1. / 60;
            check(std::chrono::steady_clock::now() < deadline,
                  "Reflection scene resources stalled");
        } while (owner.pending());
        if (!warning && !owner.diagnostics().empty())
            throw std::runtime_error("Reflection scene: " + owner.diagnostics().front().text);
        return pixels;
    };
    auto disabled = scene;
    disabled.settings.screen_space_reflections.enabled = false;
    const auto reference = render(ordinary, disabled), reflected = render(frame, scene);
    std::size_t changes = 0;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        changes += reference[i] != reflected[i];
        check(reference[i][3] == reflected[i][3], "Reflection Game changed current alpha");
        if (reference[i] == reference.front())
            check(reflected[i] == reference[i], "Reflection Game changed clear background");
    }
    std::cout << "Actual reflection Game changed " << changes << " pixels\n" << std::flush;
    check(changes > 100, "Actual Game has no visible reflection signal");
    check(render(frame, scene) == reflected, "Spatial reflection Game changed a static frame");
    for (unsigned zero = 0; zero < 4; ++zero) {
        auto bypass = scene;
        auto& settings = bypass.settings.screen_space_reflections;
        if (zero == 0)
            settings.enabled = false;
        if (zero == 1)
            settings.max_distance = 0;
        if (zero == 2)
            settings.strength = 0;
        if (zero == 3)
            settings.max_roughness = 0;
        check(render(frame, bypass) == reference, "Reflection zero path changed ordinary Game");
    }
    for (bool color_clear : {false, true}) {
        auto load = scene;
        if (color_clear)
            load.cameras[0].camera.clear_color = false;
        else
            load.cameras[0].camera.clear_depth = false;
        FrameRenderer fallback(presentation), baseline(presentation);
        fallback.resources(host);
        baseline.resources(host);
        const auto pixels = render(fallback, load, 320, 180, true);
        check(std::any_of(fallback.diagnostics().begin(), fallback.diagnostics().end(),
                          [](const auto& d) {
                              return d.category == "render.temporal.unavailable" &&
                                     d.text.find("clear") != std::string::npos;
                          }),
              "Reflection load fallback lacks clear prerequisite warning");
        load.settings.screen_space_reflections.enabled = false;
        check(pixels == render(baseline, load),
              "Reflection rejected camera changed ordinary pixels");
    }
    const AssetRef<MaterialAsset> overlay_material{AssetId::generate()};
    MaterialResourceData overlay_surface;
    overlay_surface.values.model = "forge.gltf.unlit.v1";
    overlay_surface.values.double_sided = true;
    overlay_surface.values.depth_write = false;
    overlay_surface.values.alpha = MaterialAlpha::Blend;
    overlay_surface.values.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4,
                                                            {1, 0, 1, 1}};
    overlay_surface.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    overlay_surface.textures["baseColorTexture"] = engine_texture(EngineTexture::White);
    host->preview_material(overlay_material, overlay_surface);
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = overlay_material;
    sprite.renderer.order = 100;
    sprite.renderer.width = sprite.renderer.height = 2;
    auto layered = scene;
    layered.sprites = {sprite};
    auto layered_plain = layered;
    layered_plain.settings.screen_space_reflections.enabled = false;
    const auto overlay_on = render(frame, layered), overlay_off = render(ordinary, layered_plain);
    unsigned overlay_pixels = 0;
    for (std::size_t at = 0; at < overlay_on.size(); ++at) {
        const auto& pixel = overlay_off[at];
        if (pixel[0] > 150 && pixel[1] < 40 && pixel[2] > 150) {
            ++overlay_pixels;
            check(overlay_on[at] == pixel,
                  "Reflections changed current Sprite color, alpha or restored depth");
        }
    }
    check(overlay_pixels > 100, "Reflection current-layer fixture has no visible Sprite");
    const AssetRef<MaterialAsset> interleaved_material{AssetId::generate()};
    overlay_surface.values.alpha = MaterialAlpha::Opaque;
    overlay_surface.values.depth_write = true;
    host->preview_material(interleaved_material, overlay_surface);
    layered.sprites[0].renderer.material = interleaved_material;
    layered.sprites[0].renderer.order = -1;
    const auto interleaved = render(frame, layered, 320, 180, true);
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) {
                          return d.category == "render.temporal.unavailable" &&
                                 d.text.find("ordinary draw ordering") != std::string::npos;
                      }),
          "Reflection interleaved queue lacked ordinary-order fallback");
    layered.settings.screen_space_reflections.enabled = false;
    check(interleaved == render(ordinary, layered),
          "Reflection fallback reordered current Sprite/Mesh geometry");
    auto combined = scene;
    combined.settings.ambient_occlusion = {true, .7f, 1, .01f};
    combined.settings.temporal_anti_aliasing = {true, .8f};
    combined.settings.motion_blur = {true, .5f, 16};
    combined.settings.depth_of_field = {true, 7, 2, 4};
    combined.settings.auto_exposure.enabled = true;
    combined.settings.bloom = {true, .2f, .3f, 2};
    const TemporalFrameSample sample{AssetId::generate(), .25};
    for (unsigned i = 0; i < 8; ++i)
        (void)render(frame, combined, 320, 180, false, &sample);
    check(frame.temporal_ready(), "Reflection composition blocked explicit-sample readiness");
    auto split = scene;
    split.cameras[0].camera.viewport_width = .5;
    auto second = split.cameras[0];
    second.entity = EntityId::generate();
    second.camera.viewport_x = .5;
    second.camera.vertical_fov = .9;
    split.cameras.push_back(second);
    const auto joined = render(frame, split, 640, 180);
    auto right = scene;
    right.cameras[0] = second;
    right.cameras[0].camera.viewport_x = 0;
    right.cameras[0].camera.viewport_width = 1;
    const auto a = render(ordinary, scene), b = render(ordinary, right);
    for (unsigned y = 0; y < 180; ++y)
        for (unsigned x = 0; x < 320; ++x) {
            check(joined[y * 640 + x] == a[y * 320 + x],
                  "Reflection left camera crossed rectangle");
            check(joined[y * 640 + 320 + x] == b[y * 320 + x],
                  "Reflection right camera crossed rectangle");
        }
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        const auto on = render(frame, scene, width, height),
                   off = render(ordinary, disabled, width, height);
        std::size_t changed = 0;
        for (std::size_t i = 0; i < on.size(); ++i) {
            changed += on[i] != off[i];
            check(on[i][3] == off[i][3], "Reflection Gallery changed current alpha");
            if (off[i] == off.front())
                check(on[i] == off[i], "Reflection Gallery changed clear background");
        }
        for (bool enabled : {true, false}) {
            const auto path =
                images /
                (std::string("reflection-gallery-") + (enabled ? "enabled-" : "disabled-") +
                 std::to_string(width) + "x" + std::to_string(height) + ".ppm");
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto& pixel : enabled ? on : off)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Reflection Gallery image write failed");
        }
        check(changed > 100, "Reflection Gallery has no visible reflected color");
        std::cout << "Gallery106 " << width << 'x' << height << ": " << changed
                  << " reflected pixels PASS\n"
                  << std::flush;
    }
    forge::Viewport preview(presentation, true);
    preview.resources(host);
    preview.preview_lighting(false);
    EditorCamera orbit;
    orbit.distance = 8;
    orbit.yaw = 0;
    orbit.pitch = -.34;
    orbit.target = {0, .5f, 1.5f};
    const auto document = authored.preview_document(authored.document());
    test_sprite_draw::Pixels scene_on;
    for (unsigned i = 0; i < 20; ++i) {
        presentation.begin_frame();
        auto* result = preview.render(context, document, 320, 180, orbit, 1, false, {false});
        presentation.end_frame(context);
        host->submit();
        scene_on =
            test_sprite_draw::read_pixels(presentation.device(), context, result->GetTexture());
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    }
    check(preview.retained > 0 && preview.meshes()->diagnostics().empty(),
          "Spatial reflection Scene failed retained rendering");
    auto plain_document = document;
    plain_document["rendering"]["screen_space_reflections"]["enabled"] = false;
    forge::Viewport reference_preview(presentation, true);
    reference_preview.resources(host);
    reference_preview.preview_lighting(false);
    test_sprite_draw::Pixels scene_off;
    for (unsigned i = 0; i < 3; ++i) {
        presentation.begin_frame();
        auto* result =
            reference_preview.render(context, plain_document, 320, 180, orbit, 1, false, {false});
        presentation.end_frame(context);
        host->submit();
        scene_off =
            test_sprite_draw::read_pixels(presentation.device(), context, result->GetTexture());
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    }
    std::size_t scene_changes = 0;
    for (std::size_t i = 0; i < scene_on.size(); ++i) {
        scene_changes += scene_on[i] != scene_off[i];
        check(scene_on[i][3] == scene_off[i][3], "Reflection Scene changed alpha");
    }
    std::cout << "Actual reflection Scene changed " << scene_changes << " pixels\n" << std::flush;
    check(scene_changes > 20, "Actual Scene reflection controls have no rendered signal");
    context->WaitForIdle();
    std::cout << "Reflection Game/Scene/gallery, static/zero/alpha/background, camera "
                 "clear/rectangle and combined capture PASS\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Expected Gallery project, capture directory and scene ID");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        check(device && context, "Reflection Scene Vulkan device unavailable");
        forge::DiligentPresentation presentation(device);
        exercise(presentation, context, std::filesystem::absolute(argv[1]),
                 std::filesystem::absolute(argv[2]), forge::AssetId::parse(argv[3]));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
