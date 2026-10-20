#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "mesh_render_host.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_document.hpp"
#include "tile_set_cook.hpp"
#include "tile_set_document.hpp"
#include <iostream>
#include <thread>

using namespace forge;
using namespace Diligent;
using test_tile_asset_io::check;
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected import worker and scratch directory");
        const std::filesystem::path root = argv[2];
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "Assets");
        auto lease = std::make_shared<ProjectLease>(root);
        const auto material = test_tile_asset_io::material(root, lease);
        auto set = TileSetDocument::create(lease, "Assets/Sheet.tileset.json");
        set->edit(set->revision(), "Original definitions", [&](auto& doc) {
            doc["next_id"] = 4;
            doc["images"] = {{{"id", 1},
                              {"name", "Checker sheet"},
                              {"material", material},
                              {"grid",
                               {{"columns", 4},
                                {"rows", 4},
                                {"margin_x", 0},
                                {"margin_y", 0},
                                {"spacing_x", 0},
                                {"spacing_y", 0}}}}};
            doc["tiles"] = {{{"id", 2}, {"name", "Ink"}, {"image", 1}, {"frame", 0}},
                            {{"id", 3}, {"name", "Black"}, {"image", 1}, {"frame", 1}}};
        });
        set->save();
        AssetImportService importer(lease, tile_asset_import_registry(argv[1]),
                                    {"linux", "none", "cpu"});
        const auto publish = [&](const auto& document, bool is_map) {
            const auto receipt = test_tile_asset_io::publish(importer, document->locator(),
                                                             document->source().asset(), is_map);
            check(receipt.published, "Tile publication failed: " + receipt.diagnostic);
        };
        publish(set, false);
        auto map = TileMapDocument::create(lease, "Assets/Map.tilemap.json", [&](AssetId id) {
            return TileMapSource{TileMapValue::create(id, set->source().asset())};
        });
        const std::array<TileMapCellEdit, 1> ink{{{1, {0, 0}, {2, 0}}}};
        const std::array<TileMapCellEdit, 1> black{{{1, {0, 0}, {3, 0}}}};
        map->edit_source(map->revision(), "Paint",
                         [&](auto& value) { value.document = value.document.paint(ink); });
        map->save();
        publish(map, true);
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo create;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(create, &device, &context);
        check(device && context, "Vulkan device unavailable");
        DiligentPresentation presentation(device);
        auto host = std::make_shared<MeshResourceHost>(presentation, context, root);
        const auto catalog = [&] {
            host->catalog(std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root)));
        };
        catalog();
        auto renderer = std::make_unique<MeshSceneRenderer>(host);
        RenderScene scene;
        scene.scene = AssetId::generate();
        RenderTileMap actor;
        actor.entity = EntityId::generate();
        actor.renderer.map.id = map->source().asset();
        actor.world.m[11] = 2;
        scene.tile_maps.push_back(actor); // No provided cells/definitions: ordinary assets.
        const auto settle = [&] {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            do {
                renderer->update(scene);
                if (!renderer->pending())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (std::chrono::steady_clock::now() < end);
            throw std::runtime_error("Tile asset rendering did not settle");
        };
        RefCntAutoPtr<ITexture> color, depth;
        TextureDesc desc;
        desc.Name = "Published Tile Map proof";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = desc.Height = 32;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        device->CreateTexture(desc, nullptr, &color);
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(desc, nullptr, &depth);
        check(color && depth, "Tile targets unavailable");
        Camera settings;
        settings.projection = unsigned(CameraProjection::Orthographic);
        settings.orthographic_height = 1;
        AffineTransform camera;
        camera.m[3] = camera.m[7] = .5;
        // The fixture material is single-sided: view the Quad's positive-Z face.
        camera.m[0] = camera.m[10] = -1;
        camera.m[11] = 4;
        const auto view = camera_view(settings, camera, 32, 32);
        const auto render = [&] {
            presentation.begin_frame();
            auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
            context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[]{.125f, .25f, .5f, 1};
            context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                       RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const Diligent::Viewport area{0, 0, 32, 32, 0, 1};
            context->SetViewports(1, &area, 32, 32);
            renderer->draw(scene, view, UINT32_MAX, color, dsv);
            presentation.end_frame(context);
            host->submit();
            auto pixels = test_sprite_draw::read_pixels(device, context, color);
            context->FinishFrame();
            device->ReleaseStaleResources();
            return pixels;
        };
        const auto expect = [&](std::array<std::uint8_t, 4> pixel) {
            const auto pixels = render();
            check(std::all_of(pixels.begin(), pixels.end(),
                              [&](auto value) { return value == pixel; }),
                  "Published/preview Tile Map pixels differ: expected " +
                      nlohmann::json(pixel).dump() + ", first " +
                      nlohmann::json(pixels.front()).dump() + ", calls " +
                      std::to_string(renderer->draw_stats().calls));
        };
        settle();
        check(renderer->diagnostics().empty() && host->inspect().tile_maps.selected == 1 &&
                  host->inspect().tile_sets.selected == 1 &&
                  renderer->pick(scene, view, 16, 16) == actor.entity,
              "Ordinary Tile Map/Set loading or inspection/selection failed");
        expect({255, 0, 255, 255});
        set->edit(set->revision(), "Rename stable tile",
                  [](auto& doc) { doc["tiles"][0]["name"] = "Renamed Ink"; });
        set->save();
        publish(set, false);
        catalog();
        settle();
        check(!renderer->diagnostics().empty() && !renderer->pending(),
              "Stale Map dependency was silently adopted or reported as loading");
        expect({255, 0, 255, 255});
        publish(map, true);
        catalog();
        settle();
        check(renderer->diagnostics().empty(), "Tile asset load could not recover after recook");
        expect({255, 0, 255, 255});
        // Unsaved editor data is accepted only by an explicitly isolated host.
        const auto definitions = std::make_shared<const TileSetData>(
            decode_cooked_tile_set(cook_tile_set(set->source()), set->source().asset()));
        const auto draft = map->source().document.paint(black);
        const auto preview = std::make_shared<const TileMapData>(
            decode_cooked_tile_map(cook_tile_map(draft), map->source().asset()));
        bool rejected = false;
        try {
            host->preview_tile_map(actor.renderer.map, preview);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected, "Ordinary host accepted unsaved tile data");
        host = std::make_shared<MeshResourceHost>(presentation, context, root, true);
        catalog();
        host->preview_tile_map(actor.renderer.map, preview);
        host->preview_tile_set({set->source().asset()}, definitions);
        renderer = std::make_unique<MeshSceneRenderer>(host);
        settle();
        check(renderer->diagnostics().empty(), "Unsaved tile preview failed");
        expect({0, 0, 0, 255});
        const auto disk = TileMapSource::parse(
            asset_detail::read_bytes(root / map->locator(), tile_map_source_byte_limit));
        check(disk.document.cell(1, {}).tile == 2, "Preview changed authored cells on disk");
        // A missing running snapshot must never fall back to authored/preview cells.
        scene.tile_maps[0].runtime_instance = true;
        scene.tile_maps[0].pending = true;
        renderer = std::make_unique<MeshSceneRenderer>(host);
        renderer->update(scene);
        check(renderer->pending() && renderer->bundle_count() == 0,
              "Missing runtime cells silently fell back to an authoring asset");
        std::cout << "Published tile assets and isolated unsaved previews rendered correctly\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
