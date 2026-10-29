#pragma once
#include "editor/viewport.hpp"
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <fstream>
#include <thread>

namespace forge::test_focus_scene {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& project) {
    using test_sprite_draw::check;
    using test_sprite_draw::Pixels;
    auto* device = presentation.device();
    std::filesystem::create_directories(project);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(project));
    const auto material = engine_material();
    MaterialResourceData surface;
    surface.values.model = "forge.gltf.unlit.v1";
    surface.values.double_sided = true;
    surface.values.textures["baseColorTexture"].semantic = TextureSemantic::Color;
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(material, surface);
    RenderScene scene;
    scene.scene = AssetId::generate();
    scene.settings.shadows.enabled = false;
    scene.settings.environment.sky = false;
    RenderCamera camera{EntityId::generate(), {}, {}};
    camera.world.m[11] = -4;
    scene.cameras.push_back(camera);
    RenderMesh cube{EntityId::generate(), {}, {}};
    cube.renderer.mesh = engine_primitive(0);
    cube.renderer.materials = {{"surface", material}};
    scene.meshes.push_back(cube);
    FrameRenderer frame(presentation);
    frame.resources(host);
    double seconds = 1;
    const auto render = [&](FrameRenderer& renderer, const RenderScene& input, unsigned width = 122,
                            unsigned height = 81, bool warning = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        Pixels pixels;
        do {
            presentation.begin_frame();
            auto* output = renderer.game(context, input, width, height, seconds);
            presentation.end_frame(context);
            host->submit();
            pixels = test_sprite_draw::read_pixels(device, context, output->GetTexture());
            context->FinishFrame();
            device->ReleaseStaleResources();
            seconds += 1. / 60;
            check(std::chrono::steady_clock::now() < deadline,
                  "Focus Scene resource admission stalled");
        } while (renderer.pending());
        if (!warning && !renderer.diagnostics().empty())
            throw std::runtime_error(renderer.diagnostics().front().text);
        return pixels;
    };
    const auto ordinary = render(frame, scene);
    auto focused = scene;
    focused.settings.depth_of_field = {true, 10, 1, 6};
    const auto blurred = render(frame, focused);
    unsigned changed = 0;
    for (std::size_t i = 0; i < ordinary.size(); ++i)
        if (ordinary[i] != blurred[i])
            ++changed;
    check(changed > 50, "Actual Game focus pass did not change the textured Mesh");
    focused.settings.depth_of_field.focus_distance = 3.5f;
    check(render(frame, focused) == ordinary, "In-focus planar Game surface did not remain exact");
    focused.settings.depth_of_field.focus_distance = 10;
    focused.settings.depth_of_field.blur_radius = 0;
    check(render(frame, focused) == ordinary, "Zero-radius Game path changed ordinary pixels");
    const auto no_depth = engine_material(EngineMaterial::TwoSided);
    auto incomplete_surface = surface;
    incomplete_surface.values.depth_write = false;
    host->preview_material(no_depth, incomplete_surface);
    auto incomplete = scene;
    incomplete.meshes[0].renderer.materials = {{"surface", no_depth}};
    incomplete.settings.depth_of_field = {true, 10, 1, 6};
    const auto rejected_depth = render(frame, incomplete, 122, 81, true);
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) {
                          return d.category == "render.focus.unavailable" &&
                                 d.text.find("write depth") != std::string::npos;
                      }),
          "Missing Mesh depth did not explain focus fallback");
    incomplete.settings.depth_of_field.enabled = false;
    check(rejected_depth == render(frame, incomplete),
          "Missing-depth focus fallback changed ordinary pixels");
    focused.settings.depth_of_field.blur_radius = 6;
    focused.cameras[0].camera.viewport_width = .5;
    auto second = focused.cameras[0];
    second.entity = EntityId::generate();
    second.camera.viewport_x = .5;
    second.camera.projection = 1;
    second.camera.orthographic_height = 2;
    focused.cameras.push_back(second);
    const auto split = render(frame, focused);
    auto left = focused;
    left.cameras.resize(1);
    left.cameras[0].camera.viewport_width = 1;
    auto right = left;
    right.cameras[0] = second;
    right.cameras[0].camera.viewport_x = 0;
    right.cameras[0].camera.viewport_width = 1;
    FrameRenderer reference(presentation);
    reference.resources(host);
    const auto left_pixels = render(reference, left, 61, 81),
               right_pixels = render(reference, right, 61, 81);
    for (unsigned y = 0; y < 81; ++y)
        for (unsigned x = 0; x < 61; ++x) {
            check(split[y * 122 + x] == left_pixels[y * 61 + x],
                  "Left focus view sampled another camera rectangle");
            check(split[y * 122 + x + 61] == right_pixels[y * 61 + x],
                  "Right focus view sampled another camera rectangle");
        }
    auto load = left;
    load.cameras[0].camera.clear_depth = false;
    const auto fallback = render(frame, load, 61, 81, true);
    check(std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(),
                      [](const auto& d) {
                          return d.category == "render.focus.unavailable" &&
                                 d.severity == Severity::Warning;
                      }),
          "Focus camera load fallback lacked an actionable warning");
    load.settings.depth_of_field.enabled = false;
    check(fallback == render(frame, load, 61, 81),
          "Focus camera load fallback changed ordinary pixels");
    auto layered = scene;
    layered.settings.depth_of_field = {true, 10, 1, 6};
    const AssetRef<MaterialAsset> sprite_material{AssetId::generate()};
    host->preview_material(sprite_material, surface);
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = sprite_material;
    sprite.renderer.order = -1;
    sprite.renderer.width = sprite.renderer.height = 2;
    sprite.world.m[11] = -.8;
    layered.sprites.push_back(sprite);
    const auto ordered = render(frame, layered, 122, 81, true);
    const bool order_warning =
        std::any_of(frame.diagnostics().begin(), frame.diagnostics().end(), [](const auto& d) {
            return d.category == "render.focus.unavailable" &&
                   d.text.find("ordinary draw ordering") != std::string::npos;
        });
    if (!order_warning) {
        std::string details;
        for (const auto& diagnostic : frame.diagnostics())
            details += diagnostic.category + ": " + diagnostic.text + "; ";
        throw std::runtime_error(
            "Interleaved focus layers did not retain the existing queue; actual: " + details);
    }
    layered.settings.depth_of_field.enabled = false;
    check(ordered == render(frame, layered),
          "Focus fallback reordered opaque Sprite/Mesh geometry");
    const AssetRef<MaterialAsset> overlay_material{AssetId::generate()};
    surface.values.alpha = MaterialAlpha::Blend;
    surface.values.depth_write = false;
    host->preview_material(overlay_material, surface);
    layered = scene;
    sprite.renderer.material = overlay_material;
    sprite.renderer.order = 100;
    sprite.renderer.width = sprite.renderer.height = 1;
    layered.sprites = {sprite};
    const auto overlay = render(frame, layered);
    layered.settings.depth_of_field = {true, 10, 1, 6};
    const auto focused_overlay = render(frame, layered);
    for (unsigned y = 32; y < 49; ++y)
        for (unsigned x = 52; x < 70; ++x)
            check(overlay[y * 122 + x] == focused_overlay[y * 122 + x],
                  "Current-frame Sprite was blurred or lost restored camera depth/targets");
    auto combined = scene;
    combined.settings.depth_of_field = {true, 10, 1, 6};
    combined.settings.temporal_anti_aliasing = {true, .8f};
    const TemporalFrameSample sample{AssetId::generate(), .25};
    for (unsigned i = 0; i < 8; ++i) {
        presentation.begin_frame();
        auto* output = frame.game(context, combined, 122, 81, seconds + 100 * i, &sample);
        presentation.end_frame(context);
        host->submit();
        (void)test_sprite_draw::read_pixels(device, context, output->GetTexture());
        context->FinishFrame();
        device->ReleaseStaleResources();
        check(frame.diagnostics().empty(), "Combined temporal/focus frame emitted a diagnostic");
    }
    check(frame.temporal_ready(), "Spatial focus pass blocked temporal explicit-sample readiness");
    EngineContext engine;
    Scene authored(engine.world());
    std::string entity;
    for (unsigned y = 0; y < 5; ++y)
        for (unsigned x = 0; x < 5; ++x) {
            const auto created = authoring_command(
                authored, "entity.create",
                {{"kind", 0},
                 {"position",
                  {{"x", (double(x) - 2) * .4}, {"y", (double(y) - 2) * .4}, {"z", 0}}}});
            const std::string id = created.at("selected");
            if (x == 2 && y == 2)
                entity = id;
            auto document = authored.document();
            for (auto& row : document["entities"])
                if (row.at("id") == id) {
                    row["components"]["forge.local_scale"] = {{"x", .4}, {"y", .4}, {"z", .1}};
                    row["components"]["forge.tint"] = (x + y) % 2
                                                          ? Json{{"r", .95}, {"g", .5}, {"b", .09}}
                                                          : Json{{"r", .04}, {"g", .35}, {"b", .8}};
                }
            authored.edit(document);
        }
    Viewport preview(presentation, true);
    preview.resources(host);
    preview.preview_lighting(true);
    EditorCamera orbit;
    orbit.target = {0, 0, 0};
    orbit.yaw = orbit.pitch = 0;
    orbit.distance = 4;
    const auto preview_frame = [&](unsigned generation) {
        presentation.begin_frame();
        auto* output = preview.render(context, authored.effective_document(), 122, 81, orbit,
                                      generation, false, {false});
        presentation.end_frame(context);
        host->submit();
        auto pixels = test_sprite_draw::read_pixels(device, context, output->GetTexture());
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    Pixels scene_pixels;
    for (unsigned i = 0; i < 16; ++i)
        scene_pixels = preview_frame(1);
    check(!preview.meshes()->pending() && preview.meshes()->diagnostics().empty(),
          "Scene focus fixture did not finish loading");
    check(std::count_if(scene_pixels.begin(), scene_pixels.end(),
                        [&](const auto& pixel) { return pixel != scene_pixels.front(); }) > 100,
          "Scene focus fixture has no visible centered geometry");
    authoring_command(
        authored, "scene.rendering.set",
        {{"depth_of_field",
          {{"enabled", true}, {"focus_distance", 10}, {"focus_range", 1}, {"blur_radius", 6}}}});
    const auto scene_focused = preview_frame(2);
    if (scene_focused == scene_pixels || !preview.meshes()->diagnostics().empty()) {
        std::string details;
        for (const auto& diagnostic : preview.meshes()->diagnostics())
            details += diagnostic.category + ": " + diagnostic.text + "; ";
        throw std::runtime_error("Actual Scene viewport focus changed=" +
                                 std::to_string(scene_focused != scene_pixels) + "; " + details);
    }
    const auto redraws = preview.redraws;
    check(preview_frame(2) == scene_focused && preview.redraws == redraws,
          "Stateless focus pass invalidated retained Scene frames");
    check(preview.pick(authored.effective_document(), orbit, 122, 81, 61, 40.5) == entity,
          "Focus filtering changed Scene picking");
    std::cout
        << "Actual Game/Scene focus, sharp plane, disabled/zero, split views, load/order fallback, "
           "current-frame Sprite, temporal capture readiness and retained Scene/picking PASS\n";
}
inline void gallery(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                    const std::filesystem::path& project, const std::filesystem::path& images) {
    using test_sprite_draw::check;
    const auto id = AssetId::parse("4af485f2-660b-45e6-a9d5-a33a80ba8983");
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    EngineContext engine(WorldRole::Runtime, false);
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(project, {id}));
    RuntimeSimulation simulation(engine.world(), scene);
    simulation.reset_presentation();
    auto rendered = extract_render_scene(simulation.presentation(1));
    check(rendered.meshes.size() == 192, "Focus Gallery lost its original checker geometry");
    FrameRenderer frame(presentation);
    frame.resources(host);
    std::filesystem::create_directories(images);
    for (const auto& [width, height] : {std::pair{640u, 360u}, std::pair{360u, 640u}}) {
        const auto render = [&](bool enabled) {
            rendered.settings.depth_of_field.enabled = enabled;
            test_sprite_draw::Pixels pixels;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            do {
                presentation.begin_frame();
                auto* output = frame.game(context, rendered, width, height, 1);
                presentation.end_frame(context);
                host->submit();
                pixels = test_sprite_draw::read_pixels(presentation.device(), context,
                                                       output->GetTexture());
                context->FinishFrame();
                presentation.device()->ReleaseStaleResources();
                check(std::chrono::steady_clock::now() < deadline,
                      "Focus Gallery resources stalled");
            } while (frame.pending());
            if (!frame.diagnostics().empty())
                throw std::runtime_error(frame.diagnostics().front().text);
            const auto name = std::string("focus-gallery-") + (enabled ? "enabled-" : "disabled-") +
                              std::to_string(width) + "x" + std::to_string(height) + ".ppm";
            std::ofstream file(images / name, std::ios::binary);
            file << "P6\n" << width << ' ' << height << "\n255\n";
            for (const auto& pixel : pixels)
                file.write(reinterpret_cast<const char*>(pixel.data()), 3);
            check(bool(file.flush()), "Actual focus Gallery image write failed");
            return pixels;
        };
        const auto ordinary = render(false), focused = render(true);
        unsigned changed = 0;
        for (std::size_t i = 0; i < ordinary.size(); ++i) {
            changed += ordinary[i] != focused[i];
            check(ordinary[i][3] == focused[i][3], "Focus Gallery changed current alpha");
        }
        check(changed > 100, "Focus Gallery did not soften its near/far checker panels");
        const auto area = frame.cameras().at(0).view.viewport;
        for (unsigned y = area.y + unsigned(area.height * .3);
             y < area.y + unsigned(area.height * .7); ++y)
            for (unsigned x = area.x + unsigned(area.width * .39);
                 x < area.x + unsigned(area.width * .61); ++x)
                check(ordinary[y * width + x] == focused[y * width + x],
                      "Focus Gallery center plane did not stay sharp");
    }
    std::cout << "Gallery103 actual Game wide/tall focus/off captures, sharp center, softened "
                 "near/far and alpha PASS\n";
}
} // namespace forge::test_focus_scene
