#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "animation_playback_access.hpp"
#include "display_resolve.hpp"
#include "engine_render_resource.hpp"
#include "frame_renderer.hpp"
#include "mesh_render_host.hpp"
#include "scene_color_pixels_tests.hpp"
#include <bit>
#include <forge/animation.hpp>
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace Diligent;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
using Pixels = std::vector<std::array<std::uint8_t, 4>>;
Pixels read(IRenderDevice* device, IDeviceContext* context, ITexture* texture) {
    auto desc = texture->GetDesc();
    desc.Usage = USAGE_STAGING;
    desc.BindFlags = BIND_NONE;
    desc.CPUAccessFlags = CPU_ACCESS_READ;
    RefCntAutoPtr<ITexture> staging;
    device->CreateTexture(desc, nullptr, &staging);
    check(bool(staging), "Material readback allocation failed");
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->CopyTexture({texture, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                          RESOURCE_STATE_TRANSITION_MODE_TRANSITION});
    context->WaitForIdle();
    MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    check(mapped.pData != nullptr, "Material readback mapping failed");
    Pixels pixels(std::size_t(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + y * desc.Width,
                    static_cast<const std::byte*>(mapped.pData) + y * mapped.Stride,
                    desc.Width * sizeof(pixels.front()));
    context->UnmapTextureSubresource(staging, 0, 0);
    return pixels;
}
std::size_t dominant(const Pixels& pixels, unsigned channel) {
    return std::count_if(pixels.begin(), pixels.end(), [channel](const auto& value) {
        return value[channel] > 30 && value[channel] > value[(channel + 1) % 3] + 12 &&
               value[channel] > value[(channel + 2) % 3] + 12;
    });
}
void display_colors(DiligentPresentation& presentation, IRenderDevice* device,
                    IDeviceContext* context) {
    DisplayResolve display(presentation);
    TextureDesc desc;
    desc.Name = "FORGE color adjustments native HDR input";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 17;
    desc.Height = 9;
    desc.Format = TEX_FORMAT_RGBA16_FLOAT;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> input;
    device->CreateTexture(desc, nullptr, &input);
    check(bool(input), "Color HDR input allocation failed");
    const auto render = [&](std::array<float, 4> rgba, float exposure,
                            const SceneColorAdjustments& settings) {
        auto* target = input->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(target, rgba.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        auto* output = display.resolve(context, input->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                       exposure, settings);
        return read(device, context, output->GetTexture());
    };
    check_scene_color_pixels(render);
    const auto original = input;
    const auto baseline = render({.5f, .25f, .125f, .375f}, 0, {});
    desc.Width = 3;
    desc.Height = 1;
    device->CreateTexture(desc, nullptr, &input);
    check(bool(input) && render({.5f, .25f, .125f, .375f}, 0, {}).size() == 3,
          "Graded display resize/rebinding failed");
    input = original;
    check(render({.5f, .25f, .125f, .375f}, 0, {}) == baseline,
          "Graded display did not restore the original view after resize");
    bool rejected = false;
    try {
        display.resolve(context, input->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE), 0,
                        {true, {1, 1, 1}, 3, 1});
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected && read(device, context, display.output()->GetTexture()) == baseline,
          "Invalid native color settings replaced the previous pixels");
    std::cout << "Actual DisplayResolve color ordering, quantized double reference, alpha, "
                 "identity/disabled, diagnostics, resize and invalid retention PASS\n";
}
void image(const std::filesystem::path& path, const Pixels& pixels) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n384 192\n255\n";
    for (const auto& value : pixels)
        file.write(reinterpret_cast<const char*>(value.data()), 3);
    check(bool(file.flush()), "Material image write failed");
}
void color_gallery(DiligentPresentation& presentation, IRenderDevice* device,
                   IDeviceContext* context, const std::filesystem::path& project,
                   const std::filesystem::path& output) {
    const auto id = AssetId::parse("7456ed81-3d3b-45a6-8c7d-6b50073ad01a");
    EngineContext engine(WorldRole::Runtime, false);
    Scene world_scene(engine.world());
    world_scene.restore_snapshot(load_game_scene(project, {id}));
    const auto source = world_scene.effective_document();
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    FrameRenderer frame(presentation);
    frame.resources(host);
    unsigned expected_cameras = 1;
    const auto render = [&](const Json& scene) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        ITextureView* result = nullptr;
        do {
            result = frame.game(context, scene, 384, 192);
            check(std::chrono::steady_clock::now() < deadline,
                  "Gallery69 color resource admission stalled");
            if (frame.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (frame.pending());
        std::string details;
        for (const auto& error : frame.diagnostics())
            details += error.text + "; ";
        check(result && frame.diagnostics().empty() && frame.cameras().size() == expected_cameras,
              "Gallery69 actual Game render: cameras=" + std::to_string(frame.cameras().size()) +
                  "; " + details);
        return read(device, context, result->GetTexture());
    };
    auto neutral_source = source;
    neutral_source["rendering"]["color_adjustments"]["enabled"] = false;
    const auto neutral = render(neutral_source);
    check(dominant(neutral, 0) > 100 && dominant(neutral, 1) > 100 && dominant(neutral, 2) > 100,
          "Gallery69 does not visibly present its colored geometry");
    const auto warm = render(source);
    const auto misses = presentation.cache_misses();
    auto gray_source = source;
    gray_source["rendering"]["color_adjustments"]["saturation"] = 0;
    const auto gray = render(gray_source);
    check(warm != neutral && gray != warm && gray != neutral,
          "Authored Gallery69 color adjustments did not affect the shared Game frame");
    check(
        std::all_of(gray.begin(), gray.end(),
                    [](const auto& pixel) { return pixel[0] == pixel[1] && pixel[1] == pixel[2]; }),
        "Scene grading omitted some geometry or background pixels");
    check(presentation.cache_misses() == misses && render(neutral_source) == neutral,
          "Color edits created shader variants or failed to restore the neutral image");
    auto split = gray_source;
    Json second;
    for (auto& row : split["entities"])
        if (row["components"].contains("forge.camera")) {
            row["components"]["forge.camera"]["viewport_width"] = .5;
            second = row;
            second["id"] = EntityId::generate();
            second["components"]["forge.camera"]["viewport_x"] = .5;
            second["components"]["forge.camera"]["background_r"] = .3;
            break;
        }
    check(!second.is_null(), "Gallery69 split-view fixture lost its camera");
    split["entities"].push_back(second);
    expected_cameras = 2;
    const auto split_gray = render(split);
    check(
        std::all_of(split_gray.begin(), split_gray.end(),
                    [](const auto& pixel) { return pixel[0] == pixel[1] && pixel[1] == pixel[2]; }),
        "Color adjustments did not cover the complete multi-camera composition");
    split["rendering"]["color_adjustments"]["enabled"] = false;
    check(render(split) != split_gray, "Split-camera grading test had no visible effect");
    expected_cameras = 1;
    check(render(neutral_source) == neutral, "Split-camera grading contaminated the next frame");
    image(output / "gallery69-neutral.ppm", neutral);
    image(output / "gallery69-warm.ppm", warm);
    image(output / "gallery69-grayscale.ppm", gray);
    image(output / "gallery69-split-grayscale.ppm", split_gray);
    std::cout << "Gallery69 actual Game color/filter/grayscale geometry, background, "
                 "neutral restoration and shader reuse PASS\n";
}
void animated_gallery(DiligentPresentation& presentation, IRenderDevice* device,
                      IDeviceContext* context, ITexture* color, ITexture* depth,
                      const std::filesystem::path& project, const std::filesystem::path& output) {
    const auto scene_id = AssetId::parse("cc8ae41c-2dd4-4477-bc4f-1a1b3272e44b");
    EngineContext engine(WorldRole::Runtime, false,
                         {runtime_resources_module(project), animation_module(project)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(project, {scene_id}));
    RuntimeSimulation simulation(engine.world(), scene);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        engine.services().resources()->synchronize();
        if (animation->prepare_initial_pose())
            break;
        check(std::chrono::steady_clock::now() < deadline, "Gallery67 native animation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto authored = scene.effective_document();
    std::vector<EntityRef> players;
    for (const auto& row : authored.at("entities"))
        if (row.at("components").contains("forge.transform_animator"))
            players.push_back({scene_id, row.at("id").get<EntityId>()});
    check(players.size() == 3, "Gallery67 native proof lost an animation/Prefab target");
    for (const auto& player : players)
        AnimationPlaybackAccess::control(*animation, player, {{"operation", "pause"}});
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    MeshSceneRenderer renderer(host);
    const auto draw = [&] {
        simulation.reset_presentation();
        const auto frame = extract_render_scene(simulation.presentation(1));
        const auto cameras = prepare_game_cameras(frame, 384, 192);
        check(frame.meshes.size() == 5 && frame.diagnostics.empty() && cameras.cameras.size() == 1,
              "Gallery67 native copied material/camera extraction failed");
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            renderer.update(frame);
            check(std::chrono::steady_clock::now() < end,
                  "Gallery67 native resource admission stalled");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        check(renderer.diagnostics().empty(), "Gallery67 native target rejected animated values");
        const auto& view = cameras.cameras[0].view;
        renderer.shadows(frame, view, UINT32_MAX);
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float background[4]{.02f, .025f, .03f, 1};
        context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        renderer.draw(frame, view, UINT32_MAX, color, dsv);
        host->submit();
        context->Flush();
        check(renderer.diagnostics().empty() && renderer.bundle_count() == 2 &&
                  renderer.draw_stats().instances == 5,
              "Gallery67 animation split shared resources or lost native draws");
        return read(device, context, color);
    };
    const auto seek = [&](double seconds) {
        for (const auto& player : players)
            AnimationPlaybackAccess::control(
                *animation, player, {{"operation", "seek"}, {"position_seconds", seconds}});
        return draw();
    };
    const auto red = seek(0);
    const auto quarter = seek(.5);
    const auto middle = seek(1);
    const auto checkpoint = animation->checkpoint();
    const auto blue = seek(2);
    check(red != quarter && quarter != middle && middle != blue &&
              dominant(red, 0) > dominant(blue, 0) + 30 &&
              dominant(blue, 2) > dominant(red, 2) + 30,
          "Gallery67 animated samples did not reach distinct native color pixels");
    animation->restore(checkpoint);
    check(draw() == middle, "Gallery67 recovery failed to restore exact animated native pixels");
    check(seek(4) == red, "Gallery67 loop-end seek failed to restore initial native pixels");
    image(output / "gallery67-start.ppm", red);
    image(output / "gallery67-quarter.ppm", quarter);
    image(output / "gallery67-middle.ppm", middle);
    image(output / "gallery67-blue.ppm", blue);
    std::cout << "Gallery67 actual animation/Prefab samples, recovery/loop pixels and two shared "
                 "bundles PASS\n";
}
void camera_shot_gallery(DiligentPresentation& presentation, IRenderDevice* device,
                         IDeviceContext* context, ITexture* color, ITexture* depth,
                         const std::filesystem::path& project,
                         const std::filesystem::path& output) {
    const auto scene_id = AssetId::parse("45697f18-c66f-407b-a04b-51f037a71f48");
    EngineContext engine(WorldRole::Runtime, false,
                         {runtime_resources_module(project), animation_module(project)});
    Scene scene(engine.world());
    scene.restore_snapshot(load_game_scene(project, {scene_id}));
    RuntimeSimulation simulation(engine.world(), scene);
    const auto animation = animation_runtime(engine.world());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!animation->prepare_initial_pose()) {
        engine.services().resources()->synchronize();
        check(std::chrono::steady_clock::now() < deadline, "Gallery68 native animation stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EntityRef controller;
    std::map<double, EntityId> cameras;
    EntityId fallback;
    const auto authored = scene.effective_document();
    for (const auto& row : authored.at("entities")) {
        const auto& c = row.at("components");
        if (c.contains("forge.animation_sequence"))
            controller = {scene_id, row.at("id").get<EntityId>()};
        if (c.contains("forge.camera_shot")) {
            const auto& shot = c.at("forge.camera_shot");
            cameras.emplace(shot.at("start_seconds").get<double>(),
                            shot.at("camera").get<EntityRef>().entity);
        }
        if (c.contains("forge.camera") && c.at("forge.camera").at("enabled").get<bool>())
            fallback = row.at("id").get<EntityId>();
    }
    check(controller.entity && fallback && cameras.size() == 3,
          "Gallery68 camera arrangement missing");
    AnimationPlaybackAccess::control(*animation, controller, {{"operation", "pause"}});
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project);
    host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(project)));
    MeshSceneRenderer renderer(host);
    const auto render = [&](const RenderScene& frame) {
        const auto views = prepare_game_cameras(frame, 384, 192);
        check(frame.diagnostics.empty() && views.diagnostics.empty() && views.cameras.size() == 1,
              "Gallery68 native camera admission failed");
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            renderer.update(frame);
            check(std::chrono::steady_clock::now() < end,
                  "Gallery68 native mesh admission stalled");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        check(renderer.diagnostics().empty(), "Gallery68 native resources rejected");
        const auto& camera = views.cameras.front();
        renderer.shadows(frame, camera.view, camera.settings.layers);
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        // Common background isolates actual camera/geometry changes in this probe.
        const float background[4]{.02f, .025f, .03f, 1};
        context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        renderer.draw(frame, camera.view, camera.settings.layers, color, dsv);
        host->submit();
        context->Flush();
        check(renderer.diagnostics().empty() && renderer.draw_stats().instances == 4,
              "Gallery68 native shot lost geometry or failed draw");
        return read(device, context, color);
    };
    const auto sample = [&](double seconds, EntityId expected) {
        AnimationPlaybackAccess::control(*animation, controller,
                                         {{"operation", "seek"}, {"position_seconds", seconds}});
        simulation.reset_presentation();
        const auto frame = extract_render_scene(simulation.presentation(1));
        check(frame.cameras.size() == 1 && frame.cameras.front().entity == expected,
              "Gallery68 selected the wrong native camera");
        auto reference = scene.effective_document();
        for (auto& row : reference["entities"])
            if (row["components"].contains("forge.camera")) {
                const auto id = row.at("id").get<EntityId>();
                check(row.at("components").at("forge.camera").at("enabled") == (id == fallback),
                      "Camera shots changed native camera intent");
                row["components"]["forge.camera"]["enabled"] = id == expected;
            }
        const auto pixels = render(frame);
        check(pixels == render(extract_render_scene(reference)),
              "Shot pixels differ from an independently selected native Camera");
        return pixels;
    };
    const auto wide = sample(0, cameras.at(0));
    const auto side = sample(2.5, cameras.at(2));
    const auto checkpoint = animation->checkpoint();
    const auto close = sample(4.5, cameras.at(4));
    const auto overview = sample(5.75, fallback);
    check(wide != side && side != close && close != overview && overview != wide,
          "Native shot camera changes did not reach distinct geometry pixels");
    animation->restore(checkpoint);
    simulation.reset_presentation();
    check(render(extract_render_scene(simulation.presentation(1))) == side,
          "Recovered camera-shot clock did not restore native pixels");
    check(sample(6, cameras.at(0)) == wide, "Camera sequence loop did not restore initial pixels");
    image(output / "gallery68-wide.ppm", wide);
    image(output / "gallery68-side.ppm", side);
    image(output / "gallery68-close.ppm", close);
    image(output / "gallery68-overview-gap.ppm", overview);
    std::cout << "Gallery68 actual camera-shot geometry pixels, independent camera oracle, "
                 "gap/recovery/loop and immutable native cameras PASS\n";
}
void execute(const std::filesystem::path& root, const std::filesystem::path& gallery = {},
             bool animation_only = false, bool camera_shots_only = false, bool color_only = false) {
    std::filesystem::create_directories(root);
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    EngineVkCreateInfo create;
    create.Features.GeometryShaders = DEVICE_FEATURE_STATE_ENABLED;
    GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
    check(device && context, "Material Vulkan device unavailable");
    std::cout << "Object material native adapter: " << device->GetAdapterInfo().Description << '\n';
    DiligentPresentation presentation(device);
    display_colors(presentation, device, context);
    auto host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
    host->catalog(std::make_shared<const AssetCatalog>(root));
    MeshSceneRenderer renderer(host);
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE object material proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = 384;
    desc.Height = 192;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Material targets unavailable");
    if (color_only) {
        color_gallery(presentation, device, context, gallery, root);
        return;
    }
    if (camera_shots_only) {
        camera_shot_gallery(presentation, device, context, color, depth, gallery, root);
        return;
    }
    if (animation_only) {
        animated_gallery(presentation, device, context, color, depth, gallery, root);
        return;
    }
    RenderScene scene;
    scene.scene = AssetId::generate();
    for (unsigned i = 0; i < 4; ++i) {
        RenderMesh mesh;
        mesh.entity = EntityId::generate();
        mesh.renderer.mesh = engine_primitive(0);
        mesh.renderer.materials = {{"surface", engine_material(EngineMaterial::LegacyBlockout)}};
        mesh.renderer.cast_shadows = mesh.renderer.receive_shadows = false;
        mesh.world.m[3] = double(i) * 2 - 3;
        scene.meshes.push_back(std::move(mesh));
    }
    // Fix the object submission order so the no-override neighbor follows an
    // overridden object on the very same shared physical draw.
    std::sort(scene.meshes.begin(), scene.meshes.end(),
              [](const auto& a, const auto& b) { return a.entity < b.entity; });
    for (unsigned i = 0; i < 4; ++i)
        scene.meshes[i].world.m[3] = double(i) * 2 - 3;
    const auto red = MaterialParameterOverride{
        "surface", "baseColorFactor", {MaterialParameterType::LinearColor4, {1, .01f, .01f, 1}}};
    auto green = red;
    green.parameter.value = {.01f, 1, .01f, 1};
    scene.meshes[0].renderer.material_parameters = {red};
    scene.meshes[1].renderer.material_parameters = {green};
    Camera camera;
    camera.projection = 1;
    camera.orthographic_height = 5;
    AffineTransform camera_world;
    camera_world.m[11] = -10;
    const auto view = camera_view(camera, camera_world, 384, 192);
    const auto admit = [&](bool errors = false) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            renderer.update(scene);
            check(std::chrono::steady_clock::now() < deadline, "Material admission timed out");
            if (renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (renderer.pending());
        check(errors || renderer.diagnostics().empty(),
              renderer.diagnostics().empty() ? "" : renderer.diagnostics().front().text);
    };
    const auto draw = [&](bool errors = false) {
        admit(errors);
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[4]{0, 0, 0, 1};
        context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        renderer.draw(scene, view, UINT32_MAX, color, dsv);
        host->submit();
        context->Flush();
        return read(device, context, color);
    };
    const auto distinct = draw();
    check(dominant(distinct, 0) > 100 && dominant(distinct, 1) > 100 &&
              renderer.bundle_count() == 1 && renderer.draw_stats().calls == 3 &&
              renderer.draw_stats().batched_calls == 1 && renderer.draw_stats().instances == 4,
          "Object values did not isolate native pixels or preserve equal/default batches");
    image(root / "distinct.ppm", distinct);
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters = {red};
    const auto equal = draw();
    check(dominant(equal, 0) > 600 && dominant(equal, 1) == 0 && renderer.bundle_count() == 1 &&
              renderer.draw_stats().calls == 1 && renderer.draw_stats().batched_calls == 1,
          "Equal object values disabled shared native instancing");
    image(root / "equal.ppm", equal);
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters.clear();
    const auto defaults = draw();
    check(dominant(defaults, 0) == 0 && dominant(defaults, 1) == 0 && defaults != equal &&
              renderer.draw_stats().calls == 1,
          "Object values leaked into the following immutable/default draw");
    image(root / "defaults.ppm", defaults);
    scene.meshes[0].renderer.material_parameters = {red};
    const auto before_invalid = draw();
    scene.meshes[0].renderer.material_parameters[0].parameter.type = MaterialParameterType::Vector4;
    const auto retained = draw(true);
    check(retained == before_invalid && !renderer.diagnostics().empty() && !renderer.pending(),
          "Invalid semantic edit lost the complete last-good draw or retried indefinitely");
    scene.meshes[0].renderer.material_parameters = {green};
    const auto recovered = draw();
    check(dominant(recovered, 1) > 100 && dominant(recovered, 0) == 0,
          "Valid values did not recover after semantic rejection");
    // Values are admitted with a replacement resource, not applied prematurely
    // to its last-good predecessor. Failed replacements retain both together.
    for (auto& mesh : scene.meshes)
        mesh.renderer.materials = {{"surface", engine_material()}};
    Light sun;
    sun.cast_shadows = false;
    sun.intensity = 1;
    AffineTransform sun_world;
    sun_world.m[0] = sun_world.m[10] = std::cos(.4);
    sun_world.m[2] = std::sin(.4);
    sun_world.m[8] = -std::sin(.4);
    scene.lights = {{EntityId::generate(), light_view(sun, sun_world)}};
    auto rough = MaterialParameterOverride{
        "surface", "roughnessFactor", {MaterialParameterType::Scalar, {.15f}}};
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters = {red, rough};
    const auto smooth = draw();
    image(root / "pbr-smooth.ppm", smooth);
    std::cout << "PBR smooth red=" << dominant(smooth, 0)
              << " calls=" << renderer.draw_stats().calls
              << " instances=" << renderer.draw_stats().instances
              << " bundles=" << renderer.bundle_count() << '\n';
    check(dominant(smooth, 0) > 100 && renderer.draw_stats().calls == 1,
          "Lit PBR object values were not consumed by the shared shader");
    for (auto& mesh : scene.meshes)
        mesh.renderer.material_parameters[1].parameter.value[0] = .9f;
    const auto matte = draw();
    check(matte != smooth && renderer.bundle_count() == 1,
          "PBR roughness changed no pixels or created per-object resource bundles");
    image(root / "pbr-roughness.ppm", matte);
    const auto before_reload = matte;
    auto incompatible = asset_detail::engine_material_resource(engine_material());
    incompatible.values.model = "forge.gltf.specular-glossiness.v1";
    incompatible.values.parameters.erase("metallicFactor");
    host->preview_material(engine_material(), incompatible);
    const auto failed_reload = draw(true);
    check(failed_reload == before_reload && !renderer.diagnostics().empty() &&
              renderer.bundle_count() == 1,
          "Incompatible resource reload mixed new resources with prior object values");
    host->preview_material(engine_material(),
                           asset_detail::engine_material_resource(engine_material()));
    check(draw() == before_reload, "Compatible resource recovery changed retained object values");
    const auto bytes = renderer.material_payload_bytes();
    check(bytes > 0 && bytes < 16 * 1024 * 1024, "Object material payload is not accounted");
    MeshSceneRenderer limited(host, TEX_FORMAT_RGBA8_UNORM, mesh_pose_payload_limit, 4096, 1);
    for (unsigned retry = 0; retry < 2; ++retry)
        limited.update(scene);
    check(!limited.diagnostics().empty() && limited.material_payload_bytes() == 0,
          "Scene material budget admitted unaccounted object allocations");
    // Exercise the shared MeshDraw path with simultaneous skin + morph data.
    // Skinned draws have no instance SRB; numeric updates must tolerate that
    // absent parity slot and preserve all existing vertex bindings.
    MeshPart part;
    part.vertices = 4;
    part.indices = {0, 1, 2, 0, 2, 3};
    part.joint_palette = {0};
    part.streams = {
        {"POSITION", 3, std::vector<float>{-.5f, -.5f, 0, -.5f, .5f, 0, .5f, .5f, 0, .5f, -.5f, 0}},
        {"NORMAL", 3, std::vector<float>{0, 0, -1, 0, 0, -1, 0, 0, -1, 0, 0, -1}},
        {"JOINTS_0", 4, std::vector<std::uint32_t>(16, 0)},
        {"WEIGHTS_0", 4, std::vector<float>{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}}};
    part.morph_targets = {
        {{"POSITION", 3, std::vector<float>{.5f, 0, 0, .5f, 0, 0, .5f, 0, 0, .5f, 0, 0}}}};
    part.bounds = mesh_bounds(part);
    MeshData mesh;
    mesh.lods = {{1, {part}}};
    mesh.morph_names = {"Shift"};
    mesh.morph_defaults = {0};
    const auto gpu = upload_mesh(device, mesh);
    auto material =
        asset_detail::engine_material_resource(engine_material(EngineMaterial::LegacyBlockout))
            .values;
    material.parameters["baseColorFactor"] = {MaterialParameterType::LinearColor4, {1, 1, 1, 1}};
    MeshDraw skin_draw(presentation, context, gpu.lods[0].parts[0], material, {},
                       TEX_FORMAT_RGBA8_UNORM, TEX_FORMAT_D32_FLOAT);
    check(!skin_draw.supports_instances(), "Skin fixture unexpectedly used static instancing");
    const std::array<AffineTransform, 1> joints{}, inverse_bind{};
    auto morph_bounds = part.bounds;
    morph_bounds.minimum[0] += .5f;
    morph_bounds.maximum[0] += .5f;
    const auto pose = prepare_skin_pose(joints, inverse_bind, part.joint_palette, morph_bounds);
    const std::array<float, 1> weights{1};
    const auto direct = [&](MeshDraw& selected, std::span<const MaterialParameterOverride> values,
                            bool depth_only) {
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(depth_only ? 0 : 1, depth_only ? nullptr : &rtv, dsv,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float black[4]{0, 0, 0, 1};
        if (!depth_only)
            context->ClearRenderTarget(rtv, black, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        selected.draw(context, {}, view, {}, nullptr, nullptr, nullptr, {}, nullptr, weights, &pose,
                      {}, values, "surface");
        context->Flush();
        return read(device, context, depth_only ? depth : color);
    };
    const auto skin_red = direct(skin_draw, std::span(&red, 1), false);
    const auto skin_green = direct(skin_draw, std::span(&green, 1), false);
    const auto skin_default = direct(skin_draw, {}, false);
    check(dominant(skin_red, 0) > 100 && dominant(skin_green, 1) > 100 &&
              dominant(skin_default, 0) == 0 && dominant(skin_default, 1) == 0,
          "Numeric material updates lost skinned/morphed geometry or leaked values");
    image(root / "skinned-morph-values.ppm", skin_green);
    material.alpha = MaterialAlpha::Mask;
    MeshDraw masked_shadow(presentation, context, gpu.lods[0].parts[0], material, {},
                           TEX_FORMAT_UNKNOWN, TEX_FORMAT_D32_FLOAT);
    auto transparent = red;
    transparent.parameter.value[3] = 0;
    const auto depth_opaque = direct(masked_shadow, std::span(&red, 1), true);
    const auto depth_cutout = direct(masked_shadow, std::span(&transparent, 1), true);
    const auto depth_restored = direct(masked_shadow, {}, true);
    const auto written = [](const Pixels& pixels) {
        return std::count_if(pixels.begin(), pixels.end(), [](const auto& value) {
            const auto depth = std::bit_cast<float>(value);
            return std::isfinite(depth) && depth < 1;
        });
    };
    check(written(depth_opaque) > 100 && written(depth_cutout) == 0 &&
              depth_restored == depth_opaque,
          "Masked shadow pass ignored object alpha or failed to restore material defaults");
    if (!gallery.empty()) {
        color_gallery(presentation, device, context, gallery, root);
        WorldContext gallery_world;
        Scene gallery_scene(gallery_world);
        gallery_scene.restore_snapshot(
            load_game_scene(gallery, {AssetId::parse("c94f7ad7-c2cf-4e7e-89f7-d32e20294036")}));
        const auto frame = extract_render_scene(gallery_scene.effective_document());
        const auto cameras = prepare_game_cameras(frame, 384, 192);
        check(frame.meshes.size() == 7 && frame.diagnostics.empty() && cameras.cameras.size() == 1,
              "Gallery66 native scene/camera extraction differs from its CPU proof");
        auto gallery_host = std::make_shared<MeshResourceHost>(presentation, context, gallery);
        gallery_host->catalog(
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(gallery)));
        MeshSceneRenderer gallery_renderer(gallery_host);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        do {
            gallery_renderer.update(frame);
            check(std::chrono::steady_clock::now() < deadline,
                  "Gallery66 native admission timed out");
            if (gallery_renderer.pending())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (gallery_renderer.pending());
        check(gallery_renderer.diagnostics().empty(),
              gallery_renderer.diagnostics().empty() ? ""
                                                     : gallery_renderer.diagnostics().front().text);
        const auto& gallery_view = cameras.cameras[0].view;
        gallery_renderer.shadows(frame, gallery_view, UINT32_MAX);
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float background[4]{.02f, .025f, .03f, 1};
        context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0, 0, 384, 192, 0, 1};
        context->SetViewports(1, &viewport, 384, 192);
        gallery_renderer.draw(frame, gallery_view, UINT32_MAX, color, dsv);
        check(gallery_renderer.diagnostics().empty(), "Gallery66 native draw/shadow failed");
        gallery_host->submit();
        context->Flush();
        const auto gallery_pixels = read(device, context, color);
        image(root / "gallery66-shared-values.ppm", gallery_pixels);
        std::cout << "Gallery66 native RGB=" << dominant(gallery_pixels, 0) << ','
                  << dominant(gallery_pixels, 1) << ',' << dominant(gallery_pixels, 2)
                  << " bundles=" << gallery_renderer.bundle_count()
                  << " instances=" << gallery_renderer.draw_stats().instances << '\n';
        check(dominant(gallery_pixels, 0) > 30 && dominant(gallery_pixels, 1) > 30 &&
                  dominant(gallery_pixels, 2) > 30 && gallery_renderer.bundle_count() == 2 &&
                  gallery_renderer.draw_stats().instances == 7,
              "Gallery66 lost native object colors or split the shared material resources");
        animated_gallery(presentation, device, context, color, depth, gallery, root);
        camera_shot_gallery(presentation, device, context, color, depth, gallery, root);
    }
    std::cout << "Native object color/roughness, default restoration, equal-value instancing, "
                 "last-good edits/reloads/recovery, bounded sparse payload, skin/morph values "
                 "and masked shadow alpha passed\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2 || argc == 3 ||
                  (argc == 4 && (std::string_view(argv[3]) == "--animation-only" ||
                                 std::string_view(argv[3]) == "--camera-shots-only" ||
                                 std::string_view(argv[3]) == "--color-only")),
              "Need material native scratch root and optional Gallery/package");
        execute(std::filesystem::path(argv[1]) / AssetId::generate().str(),
                argc >= 3 ? argv[2] : "",
                argc == 4 && std::string_view(argv[3]) == "--animation-only",
                argc == 4 && std::string_view(argv[3]) == "--camera-shots-only",
                argc == 4 && std::string_view(argv[3]) == "--color-only");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
