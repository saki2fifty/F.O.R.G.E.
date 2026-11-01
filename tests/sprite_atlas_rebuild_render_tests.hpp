#pragma once
#include "asset_bytes.hpp"
#include "frame_renderer.hpp"
#include "sprite_draw_render_tests.hpp"
#include "texture_bundle_validation.hpp"
#include <forge/scene.hpp>
#include <thread>

namespace forge::test_sprite_atlas_rebuild_render {
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& scratch) {
    using namespace Diligent;
    using test_sprite_draw::check;
    using Pixels = test_sprite_draw::Pixels;
    const auto project = scratch / AssetId::generate().str();
    std::filesystem::create_directories(project / "Assets");
    const auto texture_id = AssetId::generate();
    AssetCatalog selected(project);
    std::uint64_t generation = 0;
    // Owned cooked selection fixture isolates actual renderer admission. The
    // separate atlas pipeline test proves real source/worker/dependency cooking.
    const auto publish = [&](bool moved, bool missing) {
        TextureData texture;
        texture.width = 8;
        texture.height = 4;
        texture.format = TextureFormat::RGBA8Srgb;
        texture.alpha = TextureAlpha::Straight;
        texture.sprite_frames = {{7, {moved ? 4u : 0u, 0, 4, 4}}};
        if (!missing)
            texture.sprite_frames.push_back({UINT32_MAX, {moved ? 0u : 4u, 0, 4, 4}});
        auto& pixels = texture.subresources.emplace_back(8 * 4 * 4);
        for (unsigned y = 0; y < 4; ++y)
            for (unsigned x = 0; x < 8; ++x) {
                const bool red = moved ? x >= 4 : x < 4;
                const std::array<std::byte, 4> color{red ? std::byte{255} : std::byte{},
                                                     !red && !moved ? std::byte{255} : std::byte{},
                                                     !red && moved ? std::byte{255} : std::byte{},
                                                     std::byte{255}};
                std::copy(color.begin(), color.end(), pixels.begin() + (y * 8 + x) * 4);
            }
        ++generation;
        const auto text = nlohmann::json{{"owned_render_fixture", generation}}.dump();
        atomic_write(project / "Assets/Atlas.atlas.json", text);
        AssetBuildInput input;
        input.source_digest = asset_detail::content_digest(std::as_bytes(std::span(text)));
        input.importer = "forge.texture.atlas";
        input.importer_revision = std::string(64, 'a');
        input.output_format = "forge.texture-bundle";
        input.output_version = 2;
        input.platform = "linux";
        input.backend = "none";
        input.profile = "cpu";
        auto bytes = encode_texture(texture);
        const auto file = texture_variant_file(TextureSemantic::Color);
        TextureBundleIndex index;
        index.variants.push_back(
            {TextureSemantic::Color, file, asset_detail::content_digest(bytes), bytes.size()});
        const auto artifact = DerivedDataCache(project).publish(
            input, {{file, std::move(bytes)}, {"texture.json", encode_texture_bundle_index(index)}},
            [](const auto& a) { (void)asset_detail::validate_texture_bundle(a.files, {}, 2); });
        AssetRecord record{
            texture_id,
            TextureAsset::type,
            "Assets/Atlas.atlas.json",
            1,
            {},
            {{"forge.import",
              {{"version", 1},
               {"generation", generation},
               {"key", input.key()},
               {"source_digest", input.source_digest},
               {"importer", input.importer},
               {"importer_revision", input.importer_revision},
               {"output_format", input.output_format},
               {"output_version", 2},
               {"artifact_digest", asset_build_digest(artifact.manifest.at("files"))}}}}};
        if (selected.records().contains(texture_id))
            selected.replace(std::move(record));
        else
            selected.add(std::move(record));
        return std::make_shared<const AssetCatalog>(selected);
    };
    auto host = std::make_shared<MeshResourceHost>(presentation, context, project, true);
    host->catalog(publish(false, false));
    const AssetRef<MaterialAsset> material{AssetId::generate()};
    MaterialResourceData surface;
    surface.values.model = "forge.gltf.unlit.v1";
    surface.values.alpha = MaterialAlpha::Blend;
    surface.values.double_sided = true;
    surface.values.depth_test = surface.values.depth_write = false;
    auto& slot = surface.values.textures["baseColorTexture"];
    slot.semantic = TextureSemantic::Color;
    slot.sampler.min = slot.sampler.mag = TextureFilter::Nearest;
    surface.textures["baseColorTexture"] = {texture_id};
    host->preview_material(material, surface);
    MeshSceneRenderer renderer(host);
    RenderScene scene;
    scene.scene = AssetId::generate();
    RenderSprite sprite;
    sprite.entity = EntityId::generate();
    sprite.renderer.material = material;
    sprite.renderer.texture_frames = true;
    sprite.renderer.frame = UINT32_MAX;
    sprite.renderer.width = sprite.renderer.height = 2;
    sprite.world.m[11] = 2;
    scene.sprites.push_back(sprite);
    auto* device = presentation.device();
    TextureDesc desc;
    desc.Name = "FORGE source-aware atlas draw proof";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    RefCntAutoPtr<ITexture> color, depth;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Atlas draw targets unavailable");
    Camera camera;
    camera.projection = unsigned(CameraProjection::Orthographic);
    camera.orthographic_height = 2;
    const auto view = camera_view(camera, {}, 64, 64);
    const auto render = [&] {
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{.125f, .25f, .5f, 1};
        context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Viewport viewport{0, 0, 64, 64, 0, 1};
        context->SetViewports(1, &viewport, 64, 64);
        renderer.draw(scene, view, UINT32_MAX, color, dsv);
        presentation.end_frame(context);
        host->submit();
        return readback(color);
    };
    const auto uniform = [](std::array<std::uint8_t, 4> value) { return Pixels(64 * 64, value); };
    const auto green = uniform({0, 255, 0, 255}), red = uniform({255, 0, 0, 255}),
               blue = uniform({0, 0, 255, 255});
    const auto settle = [&](const Pixels* old_pixels = nullptr,
                            const Pixels* next_pixels = nullptr) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            renderer.update(scene);
            if (old_pixels) {
                const auto pixels = render();
                check(pixels == *old_pixels || (next_pixels && pixels == *next_pixels),
                      "Atlas preparation mixed old/new pixels and frame placement");
            }
            if (!renderer.pending())
                return;
            check(std::chrono::steady_clock::now() < deadline,
                  "Atlas draw admission did not settle");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (true);
    };
    settle();
    check(renderer.diagnostics().empty() && render() == green,
          "Sparse maximum atlas key selected incorrect native pixels");
    for (unsigned filter : {0u, 1u, 2u}) {
        scene.sprites[0].renderer.mip_filter = filter;
        settle(&green, &green);
        check(renderer.diagnostics().empty() && render() == green,
              "Atlas mip bank used a key as a physical slot");
        scene.sprites[0].renderer.frame = 7;
        settle(&green, &red);
        check(renderer.diagnostics().empty() && render() == red,
              "Numeric atlas frame edit selected the wrong mip binding");
        scene.sprites[0].renderer.frame = UINT32_MAX;
        settle(&red, &green);
        check(render() == green, "Atlas key-only edit failed to restore its image");
    }
    host->catalog(publish(true, false));
    settle(&green, &blue);
    check(renderer.diagnostics().empty() && render() == blue,
          "Repacked atlas did not atomically replace pixels and frame positions");
    scene.sprites[0].renderer.frame = 7;
    settle(&blue, &red);
    check(render() == red, "Repacked atlas lost another stable frame key");
    scene.sprites[0].renderer.frame = UINT32_MAX;
    settle(&red, &blue);
    host->catalog(publish(true, true));
    settle(&blue, &blue);
    check(!renderer.diagnostics().empty() && render() == blue,
          "Missing atlas key failed to retain the last complete native draw");
    context->WaitForIdle();
}
} // namespace forge::test_sprite_atlas_rebuild_render
