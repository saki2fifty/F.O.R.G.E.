#pragma once
#include "frame_renderer.hpp"
#include "mesh_draw_shader.hpp"
#include "render_values.hpp"
#include "sprite_draw_render_tests.hpp"
#include <fstream>
#include <limits>
#include <thread>

namespace forge::test_sprite_scene_render {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& images) {
    using namespace Diligent;
    using Json = nlohmann::json;
    using test_sprite_draw::check;
    using Pixels = test_sprite_draw::Pixels;
    std::filesystem::create_directories(images / "project");
    const auto project = images / "project";
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    auto catalog = std::make_shared<const AssetCatalog>(project);
    const auto catalog_before = catalog->document();
    host->catalog(catalog);
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialResourceData surface;
    surface.values.model = "forge.gltf.unlit.v1";
    surface.values.double_sided = true;
    surface.values.depth_test = surface.values.depth_write = false;
    surface.values.alpha = MaterialAlpha::Blend;
    auto& slot = surface.values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = TextureFilter::Nearest;
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material(material, surface);
    MeshSceneRenderer renderer(host);
    RenderScene scene;
    scene.scene = AssetId::generate();
    const auto first = EntityId::parse("71100000-0000-4000-8000-000000000001");
    const auto second = EntityId::parse("71100000-0000-4000-8000-000000000002");
    RenderSprite image;
    image.entity = first;
    image.renderer.material = material;
    image.renderer.width = image.renderer.height = 2;
    image.renderer.columns = image.renderer.rows = 4; // One pixel per checker frame.
    image.renderer.layers = 1;
    image.world.m[11] = 2;
    scene.sprites = {image};
    const auto settle = [&](MeshSceneRenderer& selected) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            selected.update(scene);
            if (!selected.pending())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Sprite scene resources did not settle");
    };
    auto* device = presentation.device();
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE Sprite scene render proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Sprite scene targets unavailable");
    Camera settings;
    settings.projection = unsigned(CameraProjection::Orthographic);
    settings.orthographic_height = 2;
    const auto view = camera_view(settings, {}, 64, 64);
    constexpr std::array<float, 4> clear{8.f / 255, 16.f / 255, 32.f / 255, 1};
    const auto render = [&](std::uint32_t layers = UINT32_MAX) {
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearRenderTarget(rtv, clear.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, 64, 64, 0, 1};
        context->SetViewports(1, &viewport, 64, 64);
        renderer.draw(scene, view, layers, color, dsv);
        presentation.end_frame(context);
        host->submit();
        auto pixels = readback(color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return pixels;
    };
    const auto save = [&](const Pixels& pixels, const char* name) {
        std::ofstream output(images / name, std::ios::binary);
        output << "P6\n64 64\n255\n";
        for (const auto& pixel : pixels)
            output.write(reinterpret_cast<const char*>(pixel.data()), 3);
        check(bool(output.flush()), "Sprite scene evidence write failed");
    };
    settle(renderer);
    check(renderer.diagnostics().empty() && renderer.bundle_count() == 1,
          "Sprite scene did not admit its ordinary material/texture selection");
    const auto magenta = render();
    check(std::all_of(
              magenta.begin(), magenta.end(),
              [](const auto& p) { return p == std::array<std::uint8_t, 4>{255, 0, 255, 255}; }),
          "Sprite scene frame pixels differ from independent source texel");
    save(magenta, "scene-sprite-frame0.ppm");
    scene.lights.resize(mesh_draw_light_limit + 1);
    settle(renderer);
    check(render() == magenta && renderer.diagnostics().empty(),
          "Unlit Sprite rejected an unrelated long 3D light list");
    scene.lights.clear();
    scene.sprites[0].renderer.frame = 1;
    settle(renderer);
    const auto black = render();
    check(black != magenta && std::all_of(black.begin(), black.end(),
                                          [](const auto& p) {
                                              return p == std::array<std::uint8_t, 4>{0, 0, 0, 255};
                                          }),
          "Sprite frame edit did not reach shared scene drawing");
    const auto source_stats = host->inspect();
    scene.sprites[0].renderer.columns = 3; // Valid component; incompatible pixel division.
    settle(renderer);
    check(!renderer.diagnostics().empty() && render() == black,
          "Incompatible atlas grid replaced a usable Sprite draw");
    scene.sprites[0].renderer.columns = 4;
    settle(renderer);
    check(renderer.diagnostics().empty() && render() == black,
          "Corrected atlas grid did not clear its error or retry values");
    scene.sprites[0].renderer.frame = 16; // Invalid direct producer value.
    settle(renderer);
    check(!renderer.diagnostics().empty() && render() == black,
          "Invalid frame damaged retained Sprite values");
    scene.sprites[0].renderer.frame = 1;
    settle(renderer);
    check(renderer.diagnostics().empty() && render() == black,
          "Returning to the previous valid frame left a stale admission error");
    scene.sprites[0].renderer.material = {AssetId::generate()};
    settle(renderer);
    check(!renderer.diagnostics().empty() && render() == black &&
              renderer.diagnostics()[0].context.property == "forge.sprite_renderer",
          "Missing Sprite material did not preserve draw/scope diagnostics");
    scene.sprites[0].renderer.material = material;
    settle(renderer);
    check(renderer.diagnostics().empty() && render() == black &&
              host->inspect().gpu_meshes.resident == source_stats.gpu_meshes.resident &&
              host->inspect().gpu_textures.resident == source_stats.gpu_textures.resident,
          "Sprite recovery duplicated native resources");
    scene.sprites[0].renderer.frame = 0;
    scene.sprites[0].renderer.height = 1;
    scene.sprites[0].renderer.pivot_x = scene.sprites[0].renderer.pivot_y = 0;
    settle(renderer);
    const auto bounds = renderer.bounds(scene, {first}, {});
    check(bounds && bounds->minimum == Double3{0, -1, 2} && bounds->maximum == Double3{2, 0, 2} &&
              renderer.pick(scene, view, 48, 48) == first && !renderer.pick(scene, view, 16, 16) &&
              !renderer.pick(scene, view, 48, 48, 2),
          "Sprite framing/picking ignored its admitted pivot/size/layers");
    const auto positioned = render();
    scene.sprites[0].renderer.flip_x = scene.sprites[0].renderer.flip_y = true;
    settle(renderer);
    check(renderer.bounds(scene, {first}, {})->minimum == bounds->minimum &&
              renderer.bounds(scene, {first}, {})->maximum == bounds->maximum &&
              render() == positioned,
          "Sprite UV flips moved single-texel geometry or pivot");
    scene.sprites[0].selectable = false;
    settle(renderer);
    check(!renderer.pick(scene, view, 48, 48), "Sprite selectability was ignored");

    // Two draw scopes on the same entity must coexist, sharing physical resources.
    scene.sprites[0] = image;
    scene.sprites[0].renderer.order = 1;
    RenderMesh mesh;
    mesh.entity = first;
    mesh.world.m[11] = 2;
    mesh.renderer.mesh = engine_primitive(8);
    mesh.renderer.materials = {{"surface", material}};
    scene.meshes = {mesh};
    settle(renderer);
    (void)render();
    check(renderer.diagnostics().empty() && renderer.bundle_count() == 2 &&
              renderer.draw_stats().instances == 2 && renderer.draw_stats().calls == 2 &&
              host->inspect().gpu_meshes.resident == 1 &&
              host->inspect().gpu_textures.resident == 1,
          "Mesh/Sprite scopes collided or their pipeline specializations duplicated resources");
    scene.meshes.clear();

    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::White);
    host->preview_material(material, surface);
    image.renderer.columns = image.renderer.rows = 1;
    image.renderer.tint_r = 1;
    image.renderer.tint_g = image.renderer.tint_b = 0;
    image.renderer.tint_a = .5;
    image.renderer.order = std::numeric_limits<std::int32_t>::max();
    image.world.m[11] = 3; // Far red draws first regardless of priority.
    auto near = image;
    near.entity = second;
    near.world.m[11] = 2;
    near.renderer.tint_r = 0;
    near.renderer.tint_b = 1;
    near.renderer.order = std::numeric_limits<std::int32_t>::min();
    scene.sprites = {near, image};
    settle(renderer);
    const auto blended = render();
    const std::array<double, 4> expected{.25 + clear[0] * .25, clear[1] * .25, .5 + clear[2] * .25,
                                         1};
    for (const auto& pixel : blended)
        for (unsigned lane = 0; lane < 4; ++lane)
            check(std::abs(int(pixel[lane]) - int(std::lround(expected[lane] * 255))) <= 1,
                  "Sprite Blend queue/instance order differs from independent alpha reference");
    check(renderer.draw_stats().calls == 1 && renderer.draw_stats().instances == 2 &&
              renderer.draw_stats().batched_calls == 1,
          "Compatible translucent sprites did not use ordered contiguous instancing");
    save(blended, "scene-sprite-blend.ppm");
    scene.sprites[1].world.m[11] = 2; // Equal depth: high-priority red now draws last.
    settle(renderer);
    check(render() != blended && renderer.pick(scene, view, 32, 32) == first,
          "Equal-depth Sprite order/picking did not follow explicit priority");
    std::reverse(scene.sprites.begin(), scene.sprites.end());
    settle(renderer);
    const auto reordered = render();
    std::reverse(scene.sprites.begin(), scene.sprites.end());
    settle(renderer);
    check(render() == reordered, "Sprite order depends on extraction order");

    scene.sprites.clear();
    for (unsigned i = 0; i < 129; ++i) {
        auto item = image;
        item.entity = EntityId::generate();
        item.world.m[11] = 2;
        item.renderer.order = int(i);
        item.renderer.tint_r = item.renderer.tint_b = 0;
        item.renderer.tint_g = float(i) / 128;
        item.renderer.tint_a = 1;
        scene.sprites.push_back(item);
    }
    settle(renderer);
    const auto many = render();
    check(renderer.draw_stats().calls == 3 && renderer.draw_stats().instances == 129 &&
              renderer.draw_stats().batched_calls == 2 && renderer.bundle_count() == 1 &&
              many[32 * 64 + 32] == std::array<std::uint8_t, 4>{0, 255, 0, 255},
          "Sprite batching crossed its bounded instance limit or lost final instance values");
    const auto bounded = host->inspect();
    for (unsigned step = 0; step < 8; ++step) {
        scene.sprites.back().renderer.tint_r = float(step) / 8;
        settle(renderer);
        (void)render();
    }
    check(host->inspect().gpu_meshes.payload_bytes == bounded.gpu_meshes.payload_bytes &&
              host->inspect().gpu_textures.payload_bytes == bounded.gpu_textures.payload_bytes &&
              renderer.bundle_count() == 1 && catalog->document() == catalog_before,
          "Sprite numeric edits grew physical resources or mutated the source catalog");
    scene.sprites.clear();
    settle(renderer);
    check(renderer.bundle_count() == 0 && renderer.pose_payload_bytes() == 0 &&
              renderer.material_payload_bytes() == 0,
          "Removed sprites retained draw bundles or admitted pose/value payloads");
    scene.sprites = {image};
    MeshSceneRenderer limited(host, TEX_FORMAT_RGBA8_UNORM, mesh_pose_payload_limit, 4096, 64);
    settle(limited);
    check(!limited.diagnostics().empty() && !limited.pending() && limited.bundle_count() == 0 &&
              limited.material_payload_bytes() == 0,
          "Sprite numeric payload bypassed its existing scene budget");

    // The actual Game consumer extracts the same authored component and uses HDR
    // plus display resolve; the raw RGBA8 checks above isolate draw correctness.
    FrameRenderer game(presentation);
    game.resources(host);
    image.renderer.tint_r = image.renderer.tint_b = 1;
    image.renderer.tint_g = 0;
    image.renderer.tint_a = 1;
    image.world.m[11] = 2;
    Json document{
        {"asset_id", scene.scene},
        {"entities",
         Json::array(
             {{{"id", first},
               {"spatial_resolved", true},
               {"world_affine", image.world.m},
               {"components", {{"forge.sprite_renderer", detail::render_value(image.renderer)}}}},
              {{"id", second},
               {"spatial_resolved", true},
               {"world_affine", AffineTransform{}.m},
               {"components", {{"forge.camera", detail::render_value(settings)}}}}})}};
    const auto game_pixels = [&] {
        Pixels result;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            presentation.begin_frame();
            auto* output = game.game(context, document, 64, 64);
            presentation.end_frame(context);
            host->submit();
            result = readback(output->GetTexture());
            context->FinishFrame();
            device->ReleaseStaleResources();
            if (!game.pending())
                return result;
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Game Sprite resources did not settle");
    };
    const auto game_image = game_pixels();
    check(game.diagnostics().empty() && game.game_scene() &&
              game.game_scene()->sprites.size() == 1 && game.bounds() &&
              game_image[32 * 64 + 32][0] > 100 && game_image[32 * 64 + 32][2] > 100,
          "Shared Game extraction/HDR/display consumer did not render the Sprite");
    save(game_image, "game-sprite.ppm");
    document["entities"][0]["components"]["forge.sprite_renderer"]["tint_r"] = 0;
    document["entities"][0]["components"]["forge.sprite_renderer"]["tint_b"] = 0;
    const auto game_black = game_pixels();
    check(game_black != game_image && game_black[32 * 64 + 32][0] == 0 &&
              game_black[32 * 64 + 32][2] == 0,
          "Sprite numeric edits did not reach the Game consumer");
}
} // namespace forge::test_sprite_scene_render
