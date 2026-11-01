#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#include "mesh_render_host.hpp"
#include "runtime_package.hpp"
#include "runtime_tile_map.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_gallery_scene_fixture.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_view.hpp"
#include <chrono>
#include <forge/runtime_world.hpp>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <thread>
using namespace forge;
using namespace Diligent;
int main(int argc, char** argv) {
    try {
        test_tile_asset_io::check(argc == 4,
                                  "Need Gallery, private scratch and matching asset worker");
        const auto prepared = test_tile_gallery_scene::prepare(
            argv[1], argv[2], argv[3], "Scenes/125-custom-polygon-one-way.scene.json");
        RuntimeWorld world({}, {}, std::nullopt, prepared.root, false);
        world.scene.restore_snapshot(load_game_scene(prepared.root, {prepared.scene}));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!world.physics()->prepare_assets() || !world.prepare_scene_resources()) {
            test_tile_asset_io::check(std::chrono::steady_clock::now() < deadline,
                                      "Budget fixture assets stalled");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto frame = extract_render_scene(world.simulation.presentation(1));
        detail::bind_runtime_tile_maps(world.engine.world(), frame);
        frame.sprites.clear();
        frame.meshes.clear();
        test_tile_asset_io::check(frame.tile_maps.size() == 1 && frame.cameras.size() == 1,
                                  "Budget fixture lost its original Map/camera");
        auto& map = frame.tile_maps.front();
        auto cells = *map.cells;
        std::vector<TileMapCellEdit> edits;
        for (int y = 1000; y < 1128; ++y)
            for (int x = 1000; x < 1128; ++x) {
                edits.push_back({1, {x, y}, {4, 0}});
                if (edits.size() == tile_map_edit_limit) {
                    cells = cells.with_cells(edits, *map.definitions);
                    edits.clear();
                }
            }
        test_tile_asset_io::check(edits.empty(), "Budget fixture edit remainder unexpected");
        map.cells = std::make_shared<const TileMapData>(std::move(cells));
        map.variants.reset();
        ++map.revision;
        auto view = camera_view(frame.cameras[0].camera, frame.cameras[0].world, 128, 72);
        std::size_t visible_chunks = 0, visible_chunk_cells = 0;
        visit_visible_tile_chunk_values(
            *map.cells, map.world, std::span(&view, 1), [&](const auto& chunk) {
                ++visible_chunks;
                visible_chunk_cells += std::count_if(chunk.cells.begin(), chunk.cells.end(),
                                                     [](auto cell) { return bool(cell.tile); });
            });
        test_tile_asset_io::check(
            map.cells->occupied() == 16389 && visible_chunk_cells == 5,
            "Budget fixture did not isolate five visible cells from the distant dense region");
        RefCntAutoPtr<IRenderDevice> device;
        RefCntAutoPtr<IDeviceContext> context;
        EngineVkCreateInfo info;
        GetEngineFactoryVk()->CreateDeviceAndContextsVk(info, &device, &context);
        test_tile_asset_io::check(device && context, "Budget fixture Vulkan device unavailable");
        DiligentPresentation presentation(device);
        auto host = std::make_shared<MeshResourceHost>(presentation, context, prepared.root);
        host->catalog(
            std::make_shared<const AssetCatalog>(AssetCatalog::open_project(prepared.root)));
        const auto budget = 64 * sizeof(AffineTransform);
        MeshSceneRenderer reference(host), limited(host, TEX_FORMAT_RGBA8_UNORM, budget);
        TextureDesc desc;
        desc.Name = "FORGE fitting Tile Map view budget proof";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = 128;
        desc.Height = 72;
        desc.Format = TEX_FORMAT_RGBA8_UNORM;
        desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        RefCntAutoPtr<ITexture> color, depth;
        device->CreateTexture(desc, nullptr, &color);
        desc.Format = TEX_FORMAT_D32_FLOAT;
        desc.BindFlags = BIND_DEPTH_STENCIL;
        device->CreateTexture(desc, nullptr, &depth);
        test_tile_asset_io::check(color && depth, "Budget fixture targets unavailable");
        std::vector<PreparedCamera> declared_views;
        std::uint32_t camera_layers = UINT32_MAX;
        const auto render = [&](MeshSceneRenderer& renderer, bool settle = true) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            test_sprite_draw::Pixels pixels;
            do {
                presentation.begin_frame();
                auto settings = frame.cameras[0].camera;
                settings.layers = camera_layers;
                const PreparedCamera prepared_view{frame.cameras[0].entity, settings, view};
                renderer.update(frame, declared_views.empty()
                                           ? std::span(&prepared_view, 1)
                                           : std::span<const PreparedCamera>(declared_views));
                auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
                auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
                context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const float background[]{.03f, .06f, .12f, 1};
                context->ClearRenderTarget(rtv, background,
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                const Viewport area{0, 0, 128, 72, 0, 1};
                context->SetViewports(1, &area, 128, 72);
                renderer.draw(frame, view, camera_layers, color, dsv);
                presentation.end_frame(context);
                host->submit();
                pixels = test_sprite_draw::read_pixels(device, context, color);
                context->FinishFrame();
                device->ReleaseStaleResources();
                test_tile_asset_io::check(std::chrono::steady_clock::now() < end,
                                          "Budget fixture draw stalled");
                if (renderer.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (settle && renderer.pending());
            return pixels;
        };
        const auto original_view = view;
        const auto full = render(reference);
        const auto small = render(limited);
        Json diagnostics = Json::array();
        for (const auto& value : limited.diagnostics())
            diagnostics.push_back(value.text);
        const bool fitting =
            limited.diagnostics().empty() && full == small && limited.draw_stats().instances == 5;
        test_tile_asset_io::check(
            fitting, "A five-cell culled view was rejected by its fitting pose budget");
        test_tile_asset_io::check(limited.pose_payload_bytes() <= budget,
                                  "Fitting Tile Map view exceeded its retained pose budget");
        const auto shared = host->inspect().tile_chunks;
        const bool coalesced = shared.selected == visible_chunks && !shared.pending &&
                               shared.memory.total() == visible_chunks * sizeof(TileMapChunkData);
        test_tile_asset_io::check(coalesced,
                                  "Shared renderers duplicated the original visible chunk pages");
        const auto fitting_bytes = limited.pose_payload_bytes();
        auto dense_camera = frame.cameras[0].world;
        const auto dense_center = tile_cell_world({1064, 1064}, map.cells->cell_size(), map.world);
        dense_camera.m[3] = dense_center.m[3];
        dense_camera.m[7] = dense_center.m[7];
        view = camera_view(frame.cameras[0].camera, dense_camera, 128, 72);
        const auto partial_reference = render(reference);
        const auto partial_instances = reference.draw_stats().instances;
        test_tile_asset_io::check(
            partial_instances > 5 &&
                partial_instances * sizeof(AffineTransform) + fitting_bytes <= budget,
            "Partial-chunk fixture does not have a fitting visible-cell workload");
        const auto partial_limited = render(limited);
        const bool partial_chunk_fits = limited.diagnostics().empty() &&
                                        partial_reference == partial_limited &&
                                        limited.draw_stats().instances == partial_instances &&
                                        limited.pose_payload_bytes() <= budget;
        test_tile_asset_io::check(
            partial_chunk_fits,
            "Off-camera cells in a visible dense chunk were charged to the pose budget");
        const auto retained_bytes = limited.pose_payload_bytes();
        auto wide_camera = frame.cameras[0].camera;
        wide_camera.orthographic_height = float(map.cells->cell_size()[1] * 64);
        view = camera_view(wide_camera, dense_camera, 128, 72);
        (void)render(reference);
        const auto oversized_instances = reference.draw_stats().instances;
        test_tile_asset_io::check(oversized_instances * sizeof(AffineTransform) > budget,
                                  "Oversized fixture camera missed its dense world-space cells");
        (void)render(limited);
        const bool oversized_rejected =
            limited.draw_stats().instances == 0 && limited.pose_payload_bytes() == retained_bytes &&
            std::any_of(limited.diagnostics().begin(), limited.diagnostics().end(),
                        [](const auto& diagnostic) {
                            return diagnostic.text ==
                                   "Tile draw transforms exceed the scene payload budget";
                        });
        test_tile_asset_io::check(
            oversized_rejected,
            "Oversized Tile Map view escaped budget admission or emitted a partial draw");
        view = original_view;
        const auto retried = render(limited);
        const bool retry = limited.diagnostics().empty() && retried == full &&
                           limited.draw_stats().instances == 5 &&
                           limited.pose_payload_bytes() == retained_bytes;
        test_tile_asset_io::check(retry, "Returning to a fitting Tile Map view did not recover "
                                         "exact pixels and retained allocation");
        (void)render(reference);
        host->pump();
        test_tile_asset_io::check(
            host->inspect().tile_chunks.selected == visible_chunks &&
                map.cells->cell(1, {1064, 1064}).tile == 4,
            "Leaving the dense region retained pages or erased logical cells");

        // Prepare the union before either camera draws, including shared owners.
        const auto partial_view = camera_view(frame.cameras[0].camera, dense_camera, 128, 72);
        declared_views = {{frame.cameras[0].entity, frame.cameras[0].camera, original_view},
                          {EntityId::generate(), frame.cameras[0].camera, partial_view}};
        std::set<TileMapChunkKey> union_keys;
        const std::array union_views{original_view, partial_view};
        visit_visible_tile_chunk_info(*map.cells, map.world, union_views,
                                      [&](const auto& chunk) { union_keys.insert(chunk.key); });
        const auto multiple_original = render(limited);
        view = partial_view;
        const auto multiple_dense = render(limited);
        const bool multiple_views = multiple_original == full &&
                                    multiple_dense == partial_limited &&
                                    limited.diagnostics().empty() &&
                                    host->inspect().tile_chunks.selected == union_keys.size();
        test_tile_asset_io::check(multiple_views,
                                  "Multiple declared cameras lost a complete chunk union");
        declared_views.clear();
        view = original_view;
        (void)render(limited);
        host->pump();

        const auto original_cells = map.cells;
        std::optional<TileMapCellEdit> erase;
        visit_visible_tile_chunk_values(
            *map.cells, map.world, std::span(&view, 1), [&](const auto& chunk) {
                for (std::uint32_t slot = 0; slot < tile_chunk_cells && !erase; ++slot)
                    if (chunk.cells[slot].tile)
                        erase = TileMapCellEdit{
                            chunk.key.layer, tile_cell_address({chunk.key.address, slot}), {}};
            });
        test_tile_asset_io::check(bool(erase), "No visible cell for replacement proof");
        map.cells = std::make_shared<const TileMapData>(
            original_cells->with_compact_cells(std::span(&*erase, 1), *map.definitions));
        ++map.revision;
        const auto loading = render(limited, false);
        const bool last_good =
            limited.pending() && loading == full && limited.draw_stats().instances == 5;
        test_tile_asset_io::check(last_good,
                                  "Pending chunk replacement did not retain the complete old draw");
        const auto edited = render(limited);
        test_tile_asset_io::check(edited != full && limited.draw_stats().instances == 4 &&
                                      limited.diagnostics().empty(),
                                  "Ready chunk replacement failed atomic adoption");

        // Supersede candidates before owner-thread adoption and drop their roots.
        map.cells = original_cells;
        ++map.revision;
        test_tile_asset_io::check(render(limited) == full, "Original page retry failed");
        for (std::uint32_t flags = 1; flags <= 7; ++flags) {
            auto write = *erase;
            write.value = original_cells->cell(write.layer, write.address);
            write.value.flags = flags;
            map.cells = std::make_shared<const TileMapData>(
                original_cells->with_compact_cells(std::span(&write, 1), *map.definitions));
            ++map.revision;
            (void)render(limited, false);
        }
        map.cells = original_cells;
        ++map.revision;
        const bool stale_safe = render(limited) == full && limited.diagnostics().empty();
        test_tile_asset_io::check(stale_safe,
                                  "Discarded candidate chunks replaced the final logical snapshot");

        camera_layers = 0;
        (void)render(reference);
        (void)render(limited);
        host->pump();
        const auto masked = host->inspect().tile_chunks;
        const bool unloaded = masked.selected == 0 && masked.retired == 0 &&
                              masked.memory.total() == 0 && !masked.pending &&
                              limited.draw_stats().instances == 0;
        test_tile_asset_io::check(unloaded, "Layer-masked views retained decoded chunk pages");
        camera_layers = UINT32_MAX;
        const bool reload = render(limited) == full && limited.draw_stats().instances == 5;
        test_tile_asset_io::check(reload, "Unloaded visible chunk pages did not reload exactly");

        bool bounded_pressure = false, pressure_recovery = false;
        std::uint64_t pressure_bytes = 0, pressure_high_water = 0;
        {
            auto demanded = frame;
            demanded.tile_maps.clear();
            for (std::uint32_t instance = 0; instance < 5; ++instance) {
                auto logical = TileMapValue::create(map.renderer.map.id, map.definitions->asset());
                std::vector<TileMapCellEdit> sparse;
                for (int y = 0; y < 32; ++y)
                    for (int x = 0; x < 32; ++x)
                        sparse.push_back({1, {x * 32, y * 32}, {4, instance}});
                logical = logical.paint(sparse);
                auto actor = map;
                actor.entity = EntityId::generate();
                actor.cells = std::make_shared<const TileMapData>(
                    decode_tile_map_archive(cook_tile_map(logical), logical.asset()).compact());
                actor.variants.reset();
                demanded.tile_maps.push_back(std::move(actor));
            }
            auto lens = frame.cameras[0].camera;
            lens.orthographic_height = float(map.cells->cell_size()[1] * 1100);
            auto eye = frame.cameras[0].world;
            const auto center = tile_cell_world({512, 512}, map.cells->cell_size(), map.world);
            eye.m[3] = center.m[3];
            eye.m[7] = center.m[7];
            PreparedCamera wide{frame.cameras[0].entity, lens, camera_view(lens, eye, 128, 72)};
            MeshSceneRenderer pressure(host);
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            do {
                presentation.begin_frame();
                pressure.update(demanded, std::span(&wide, 1));
                presentation.end_frame(context);
                host->submit();
                context->Flush(); // No readback/Present submits these preparation-only frames.
                context->FinishFrame();
                device->ReleaseStaleResources();
                const auto stats = host->inspect().tile_chunks;
                pressure_bytes = std::max(pressure_bytes, stats.memory.total());
                pressure_high_water = std::max(pressure_high_water, stats.high_water_bytes);
                if (stats.pending > 64 || stats.memory.total() > 64ull * 1024 * 1024 ||
                    std::chrono::steady_clock::now() >= end) {
                    Json errors = Json::array();
                    for (const auto& value : pressure.diagnostics())
                        errors.push_back(value.text);
                    std::ofstream progress(std::filesystem::path(argv[2]) /
                                           "pressure-progress.json");
                    progress << Json{{"selected", stats.selected},
                                     {"retired", stats.retired},
                                     {"pending", stats.pending},
                                     {"bytes", stats.memory.total()},
                                     {"high_water", stats.high_water_bytes},
                                     {"renderer_pending", pressure.pending()},
                                     {"diagnostics", errors}}
                                    .dump(2)
                             << '\n';
                }
                test_tile_asset_io::check(stats.pending <= 64 &&
                                              stats.memory.total() <= 64ull * 1024 * 1024 &&
                                              std::chrono::steady_clock::now() < end,
                                          "Chunk pressure exceeded work/byte bounds or stalled");
                if (pressure.pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (pressure.pending());
            bounded_pressure = std::any_of(
                pressure.diagnostics().begin(), pressure.diagnostics().end(),
                [](const auto& value) {
                    return value.text.find("Resource memory budget exhausted") !=
                               std::string::npos ||
                           value.text.find("Resource request/asset capacity exhausted") !=
                               std::string::npos;
                });
            test_tile_asset_io::check(
                bounded_pressure, "Oversubscribed chunk demand did not report bounded rejection");
            // Drop oversubscribed actors. Existing complete data must recover
            // without discarding the independent renderer's original-page lease.
            const PreparedCamera original{frame.cameras[0].entity, frame.cameras[0].camera, view};
            pressure.update(frame, std::span(&original, 1));
            host->pump();
            pressure.update(frame, std::span(&original, 1));
            const auto stats = host->inspect().tile_chunks;
            pressure_recovery = !pressure.pending() && pressure.diagnostics().empty() &&
                                stats.selected == visible_chunks && !stats.retired &&
                                stats.memory.total() == visible_chunks * sizeof(TileMapChunkData);
            test_tile_asset_io::check(pressure_recovery,
                                      "Released pressure failed to recover shared original pages");
        }
        std::ofstream result(std::filesystem::path(argv[2]) / "budget-result.json");
        result << Json{{"logical_cells", map.cells->occupied()},
                       {"chunks", map.cells->chunk_count()},
                       {"visible_chunks", visible_chunks},
                       {"visible_chunk_cells", visible_chunk_cells},
                       {"affine_bytes", sizeof(AffineTransform)},
                       {"pose_budget", budget},
                       {"reference_pose_bytes", reference.pose_payload_bytes()},
                       {"limited_pose_bytes", limited.pose_payload_bytes()},
                       {"reference_instances", reference.draw_stats().instances},
                       {"limited_instances", limited.draw_stats().instances},
                       {"pixels_equal", full == small},
                       {"diagnostics", diagnostics},
                       {"fitting_view", fitting},
                       {"fitting_bytes", fitting_bytes},
                       {"partial_chunk_fits", partial_chunk_fits},
                       {"partial_chunk_instances", partial_instances},
                       {"oversized_reference_instances", oversized_instances},
                       {"oversized_rejected", oversized_rejected},
                       {"small_view_retry", retry},
                       {"streamed_page_coalescing", coalesced},
                       {"multiple_camera_union", multiple_views},
                       {"pending_complete_last_good", last_good},
                       {"discarded_candidate_safe", stale_safe},
                       {"masked_pages_unloaded", unloaded},
                       {"unloaded_pages_reload", reload},
                       {"bounded_page_pressure", bounded_pressure},
                       {"page_pressure_recovery", pressure_recovery},
                       {"page_pressure_bytes", pressure_bytes},
                       {"page_pressure_high_water", pressure_high_water},
                       {"scope",
                        "Actual native software Vulkan view correctness and retained payload "
                        "accounting; not physical GPU performance or disk streaming"}}
                      .dump(2)
               << '\n';
        test_tile_asset_io::check(bool(result), "Budget fixture receipt failed");
        context->WaitForIdle();
        std::cout
            << "Tile Map view budget, oversized rejection, retry and exact reference pixels PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
