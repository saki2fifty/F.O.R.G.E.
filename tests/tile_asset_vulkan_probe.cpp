#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "asset_bytes.hpp"
#include "editor/published_tile_map.hpp"
#include "editor/viewport.hpp"
#include "mesh_render_host.hpp"
#include "runtime_tile_map.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_document.hpp"
#include "tile_map_play_receiver.hpp"
#include "tile_map_play_transport.hpp"
#include "tile_map_play_world.hpp"
#include "tile_set_cook.hpp"
#include "tile_set_document.hpp"
#include <forge/runtime.hpp>
#include <forge/runtime_resources.hpp>
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
        ui::PublishedTileMapInspection inspection;
        const auto inspect = [&] {
            auto value = inspection.select(host, actor.renderer.map);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (!value.current) {
                check(std::chrono::steady_clock::now() < deadline,
                      "Published editor Map/Set view did not become ready: " + value.status);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                value = inspection.select(host, actor.renderer.map);
            }
            return value;
        };
        const auto old_view = inspect();
        check(old_view.cells && old_view.definitions &&
                  old_view.cells->tile_set() == old_view.definitions->asset() &&
                  old_view.definitions->tile(2).collision.kind == TileCollisionKind::FullCell,
              "Published editor view did not retain its matching legacy definition snapshot");
        set->edit_source(set->revision(), "Rename and define convex tile", [](auto& source) {
            source.document["tiles"][0]["name"] = "Renamed Ink";
            source.set_collision(2,
                                 {{"kind", "convex"}, {"points", {{0., 0.}, {1., 0.}, {1., 1.}}}});
        });
        set->save();
        publish(set, false);
        catalog();
        settle();
        check(!renderer->diagnostics().empty() && !renderer->pending(),
              "Stale Map dependency was silently adopted or reported as loading");
        expect({255, 0, 255, 255});
        const auto stale_view = inspection.select(host, actor.renderer.map);
        check(!stale_view.current && stale_view.cells == old_view.cells &&
                  stale_view.definitions == old_view.definitions && !stale_view.status.empty(),
              "Editor paired stale Map cells with a newer collision definition revision");
        publish(map, true);
        catalog();
        settle();
        check(renderer->diagnostics().empty(), "Tile asset load could not recover after recook");
        const auto new_view = inspect();
        check(new_view.revision != old_view.revision &&
                  new_view.cells->tile_set() == new_view.definitions->asset() &&
                  new_view.definitions->tile(2).collision.kind == TileCollisionKind::Convex &&
                  old_view.definitions->tile(2).collision.kind == TileCollisionKind::FullCell,
              "Editor collision definitions did not advance as one immutable published pair");
        auto second_map =
            TileMapDocument::create(lease, "Assets/Second.tilemap.json", [&](AssetId id) {
                return TileMapSource{TileMapValue::create(id, set->source().asset())};
            });
        second_map->edit_source(second_map->revision(), "Second Map cells",
                                [&](auto& value) { value.document = value.document.paint(black); });
        second_map->save();
        publish(second_map, true);
        catalog();
        ui::PublishedTileMapView first_selection, second_selection;
        const auto selections_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            first_selection = inspection.select(host, actor.renderer.map);
            second_selection = inspection.select(host, {second_map->source().asset()});
            check(std::chrono::steady_clock::now() < selections_deadline,
                  "Concurrent published Map selections did not become ready");
            if (!first_selection.current || !second_selection.current)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (!first_selection.current || !second_selection.current);
        for (unsigned sample = 0; sample < 4; ++sample) {
            const auto first = inspection.select(host, actor.renderer.map);
            const auto second = inspection.select(host, {second_map->source().asset()});
            check(first.current && second.current && first.cells == first_selection.cells &&
                      second.cells == second_selection.cells && first.cells != second.cells &&
                      first.cells->cell(1, {0, 0}).tile == 2 &&
                      second.cells->cell(1, {0, 0}).tile == 3,
                  "Alternating Map guides discarded or confused published immutable selections");
        }
        {
            EngineContext author;
            Scene authored(author.world());
            authored.restore_snapshot(
                {{"version", 1},
                 {"entities",
                  Json::array({{{"id", "map"},
                                {"name", "Navigation Map"},
                                {"components",
                                 {{"forge.position", {{"x", 100}, {"y", 20}, {"z", 91}}}}}}})}});
            const auto source = authored.entity("map");
            source.set<Primitive>({no_primitive});
            source.set<TileMapRenderer>(actor.renderer);
            source.set<TileMapCollider2D>({1});
            NavigationRegion2D region;
            region.source = 1;
            source.set<NavigationRegion2D>(region);
            const auto document = authored.effective_document();
            forge::Viewport viewport(presentation);
            viewport.navigation_tiles = [&](const auto& collider, const auto& transform,
                                            bool mask) {
                const auto selected = inspection.select(host, {collider.map});
                check(selected.current, "Viewport used a pending published Map");
                return navigation_detail::tile_collider_outlines(
                    *selected.cells, *selected.definitions, selected.variants.get(), collider.layer,
                    transform, mask);
            };
            EditorCamera framed;
            framed.projection = CameraProjection::Orthographic;
            const std::vector<std::string> selection{source.get<PersistentEntityId>().value.str()};
            check(viewport.frame_selection(document, selection, framed, 1.25f) &&
                      std::abs(framed.target[0] - 100.5f) < .001 &&
                      std::abs(framed.target[1] - 20.5f) < .001 && framed.target[2] == 0,
                  "Viewport fallback did not frame the current Map collision contour in XY");
            viewport.resources(host);
            const auto frame_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            do {
                viewport.frame_selection(document, selection, framed, 1.25f);
                check(std::chrono::steady_clock::now() < frame_deadline,
                      "Viewport did not combine Map artwork and navigation bounds");
                if (framed.target[2] < 45)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (framed.target[2] < 45);
            check(std::abs(framed.target[2] - 45.5f) < .001,
                  "Viewport lost the Map's independent visual depth while framing");
        }
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
        // Follow actual native resource adoption through the same copied adapter
        // used by PlaySession, then draw its immutable values on Vulkan.
        EngineContext runtime(WorldRole::Runtime, false, {runtime_resources_module(root)});
        Scene native(runtime.world());
        auto document = native.document();
        document["asset_id"] = scene.scene;
        Json components = Json::object();
        const auto schema = native.schema();
        for (const auto& type : schema.at("components")) {
            const auto name = type.at("id").get<std::string>();
            if (name == "forge.tile_map_renderer" || name == "forge.local_translation" ||
                name == "forge.local_rotation" || name == "forge.local_scale")
                for (const auto& field : type.at("fields"))
                    components[name][field.at("id").get<std::string>()] = field.at("default");
        }
        components["forge.tile_map_renderer"]["map"] = map->source().asset();
        components["forge.local_translation"]["z"] = 2;
        document["entities"] = {
            {{"id", actor.entity}, {"name", "Copied map"}, {"components", components}}};
        native.reset(document);
        RuntimeSimulation simulation(runtime.world(), native);
        std::vector<TileMapPlayView> observed;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            scene = extract_render_scene(simulation.presentation(0));
            observed = tile_map_play_views(runtime.world());
            check(std::chrono::steady_clock::now() < end, "Native tile snapshot did not load");
            if (observed.empty() || !observed[0].cells)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (observed.empty() || !observed[0].cells);
        TileMapPlaySource sender;
        TileMapPlayReceiver receiver;
        receiver.observe(sender.capture(1, native.asset_id(), observed));
        receiver.bind(scene);
        renderer->update(scene);
        check(renderer->pending(), "Copied map rendered before transfer admission");
        expect({32, 64, 128, 255});
        const auto receive = [&] {
            while (auto request = receiver.next_request())
                receiver.receive(tile_map_play_dispatch(sender, *request));
            check(!receiver.pending(), "Copied Vulkan map transfer did not finish");
            receiver.bind(scene);
            settle();
        };
        receive();
        expect({255, 0, 255, 255});
        const auto retained = scene;
        detail::request_runtime_tile_cells(runtime.world(), "project.render_test",
                                           {native.asset_id(), actor.entity},
                                           observed[0].generation, black);
        simulation.tick(1.f / 60.f);
        scene = extract_render_scene(simulation.presentation(0));
        observed = tile_map_play_views(runtime.world());
        receiver.observe(sender.capture(1, native.asset_id(), observed));
        receive();
        expect({0, 0, 0, 255});
        scene = retained;
        settle();
        expect({255, 0, 255, 255});
        receiver.clear();
        receiver.bind(scene);
        renderer->update(scene);
        expect({32, 64, 128, 255});
        std::cout << "Published tile assets and isolated unsaved previews rendered correctly\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
