#pragma once
#include "cooked_envelope.hpp"
#include "mesh_render_host.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_map_data.hpp"
#include "view_image.hpp"
#include <forge/engine_texture_assets.hpp>
#include <thread>

namespace forge::test_tile_map_render {
inline TileMapData cells(AssetId id, AssetId definitions, std::span<const TileMapCellEdit> writes,
                         bool overlay_visible = true) {
    std::map<TileMapChunkKey, std::vector<std::pair<std::uint32_t, TileMapCell>>> chunks;
    for (const auto& write : writes) {
        const auto address = tile_chunk_cell(write.address);
        chunks[{write.layer, address.chunk}].push_back({address.index, write.value});
    }
    std::vector<std::byte> payload;
    const auto wide = [&](std::uint64_t value) {
        asset_detail::cooked_write32(payload, std::uint32_t(value));
        asset_detail::cooked_write32(payload, std::uint32_t(value >> 32));
    };
    for (auto& [key, rows] : chunks) {
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        wide(key.layer);
        asset_detail::cooked_write32(payload, std::uint32_t(key.address.x));
        asset_detail::cooked_write32(payload, std::uint32_t(key.address.y));
        asset_detail::cooked_write32(payload, std::uint32_t(rows.size()));
        for (const auto& [slot, value] : rows) {
            asset_detail::cooked_write32(payload, slot);
            wide(value.tile);
            asset_detail::cooked_write32(payload, value.flags);
        }
    }
    const nlohmann::json root{
        {"asset_id", id},
        {"tile_set", definitions},
        {"cell_size", {1., 1.}},
        {"chunk_count", chunks.size()},
        {"cell_count", writes.size()},
        {"layers",
         {{{"id", 1}, {"name", "Base"}, {"visible", true}, {"order", INT32_MIN}},
          {{"id", 2}, {"name", "Overlay"}, {"visible", overlay_visible}, {"order", INT32_MAX}}}}};
    return decode_cooked_tile_map(asset_detail::encode_envelope(root, payload,
                                                                tile_map_cooked_magic,
                                                                tile_map_cooked_metadata_limit),
                                  id);
}
template <class Readback>
void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
              Readback&& readback, const std::filesystem::path& directory) {
    using namespace Diligent;
    using test_sprite_draw::check;
    std::filesystem::create_directories(directory / "project");
    auto host =
        std::make_shared<MeshResourceHost>(presentation, context, directory / "project", true);
    host->catalog(std::make_shared<const AssetCatalog>(directory / "project"));
    const auto material = AssetId::generate(), set_id = AssetId::generate(),
               map_id = AssetId::generate();
    MaterialResourceData surface;
    surface.values.model = "forge.gltf.unlit.v1";
    surface.values.double_sided = true;
    surface.values.depth_test = surface.values.depth_write = false;
    surface.values.alpha = MaterialAlpha::Blend;
    auto& sampler = surface.values.textures["baseColorTexture"];
    sampler.semantic = TextureSemantic::Color;
    sampler.sampler.min = sampler.sampler.mag = TextureFilter::Nearest;
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material({material}, surface);
    const auto image_key = UINT64_MAX - 2, tile_key = UINT64_MAX - 1;
    const auto image = [&](std::uint64_t id) -> nlohmann::json {
        return {{"id", id},
                {"name", "Original image"},
                {"material", material},
                {"grid",
                 {{"columns", 4},
                  {"rows", 4},
                  {"margin_x", 0},
                  {"margin_y", 0},
                  {"spacing_x", 0},
                  {"spacing_y", 0}}}};
    };
    const nlohmann::json metadata{
        {"asset_id", set_id},
        {"images", {image(17), image(image_key)}},
        {"tiles",
         {{{"id", 9}, {"name", "Ink"}, {"image", 17}, {"frame", 0}},
          {{"id", tile_key}, {"name", "Black"}, {"image", image_key}, {"frame", 1}}}}};
    auto definitions = std::make_shared<const TileSetData>(
        decode_cooked_tile_set(asset_detail::encode_envelope(metadata, {}, tile_set_cooked_magic,
                                                             tile_set_cooked_metadata_limit),
                               set_id));
    const std::array<TileMapCellEdit, 4> initial{{{1, {-1, -1}, {9, 0}},
                                                  {1, {0, -1}, {tile_key, 1}},
                                                  {1, {-1, 0}, {tile_key, 4}},
                                                  {1, {0, 0}, {9, 7}}}};
    RenderScene scene;
    scene.scene = AssetId::generate();
    RenderTileMap map;
    map.entity = EntityId::generate();
    map.renderer.map.id = map_id;
    map.renderer.layers = 1;
    map.renderer.order = INT32_MAX;
    map.world.m[11] = 2;
    map.runtime_instance = true;
    map.cells = std::make_shared<const TileMapData>(cells(map_id, set_id, initial));
    map.definitions = definitions;
    scene.tile_maps.push_back(map);
    MeshSceneRenderer renderer(host);
    const auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        do {
            renderer.update(scene);
            if (!renderer.pending())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error("Tile image bundles did not settle");
    };
    auto* device = presentation.device();
    RefCntAutoPtr<ITexture> color, depth;
    TextureDesc desc;
    desc.Name = "FORGE Tile Map readback";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = desc.Height = 64;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
    device->CreateTexture(desc, nullptr, &color);
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    device->CreateTexture(desc, nullptr, &depth);
    check(color && depth, "Tile targets unavailable");
    Camera settings;
    settings.projection = unsigned(CameraProjection::Orthographic);
    settings.orthographic_height = 2;
    AffineTransform camera_world;
    const auto render = [&](std::uint32_t layers = 1) {
        presentation.begin_frame();
        auto* rtv = color->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        auto* dsv = depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        context->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float background[]{.125f, .25f, .5f, 1};
        context->ClearRenderTarget(rtv, background, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1, 0,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const Diligent::Viewport viewport{0, 0, 64, 64, 0, 1};
        context->SetViewports(1, &viewport, 64, 64);
        renderer.draw(scene, camera_view(settings, camera_world, 64, 64), layers, color, dsv);
        presentation.end_frame(context);
        host->submit();
        auto result = readback(color);
        context->FinishFrame();
        device->ReleaseStaleResources();
        return result;
    };
    settle();
    check(renderer.diagnostics().empty() && renderer.bundle_count() == 1,
          "Tile image groups did not share their ordinary Quad/material bundle");
    const auto baseline = render();
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x)
            check(baseline[y * 64 + x] == ((x >= 32) == (y < 32)
                                               ? std::array<std::uint8_t, 4>{255, 0, 255, 255}
                                               : std::array<std::uint8_t, 4>{0, 0, 0, 255}),
                  "Tile image/frame ownership or negative-grid placement differs");
    check(renderer.draw_stats().instances == 4 && renderer.draw_stats().calls == 1,
          "Tile cells did not use ordered shared instancing");
    const auto box = renderer.bounds();
    check(box && box->minimum == Double3{-1, -1, 2} && box->maximum == Double3{1, 1, 2},
          "Tile bounds include unused prototype geometry or padded chunks");
    const auto pick_view = [&] { return camera_view(settings, camera_world, 64, 64); };
    check(renderer.pick(scene, pick_view(), 16, 48, 1) == map.entity &&
              renderer.pick(scene, pick_view(), 48, 16, 1) == map.entity &&
              !renderer.pick(scene, pick_view(), 16, 48, 2),
          "Tile grid selection lost negative seams or camera masks");
    scene.tile_maps[0].selectable = false;
    check(!renderer.pick(scene, pick_view(), 16, 48), "Unselectable Tile Map was picked");
    scene.tile_maps[0].selectable = true;
    const auto masked = render(2);
    check(std::all_of(masked.begin(), masked.end(),
                      [](const auto& value) {
                          return value == std::array<std::uint8_t, 4>{32, 64, 128, 255};
                      }),
          "Tile camera mask was ignored");
    const std::array<TileMapCellEdit, 1> overlay{{{2, {0, 0}, {tile_key, 0}}}};
    scene.tile_maps[0].cells =
        std::make_shared<const TileMapData>(map.cells->with_cells(overlay, *definitions));
    settle();
    const auto layered = render();
    check(layered[16 * 64 + 48] == std::array<std::uint8_t, 4>{0, 0, 0, 255} &&
              renderer.draw_stats().instances == 5,
          "Tile layer ordering overflowed or dropped an overlapping cell");
    const auto layered_pose_bytes = renderer.pose_payload_bytes();
    std::vector<TileMapCellEdit> hidden(initial.begin(), initial.end());
    hidden.push_back({2, {0, 0}, {tile_key, 0}});
    hidden.push_back({2, {100, 100}, {tile_key, 0}});
    scene.tile_maps[0].cells =
        std::make_shared<const TileMapData>(cells(map_id, set_id, hidden, false));
    settle();
    check(render() == baseline && renderer.draw_stats().instances == 4,
          "Hidden Tile Map layer rendered occupied cells");
    check(renderer.pose_payload_bytes() == layered_pose_bytes,
          "Hidden cells reserved additional draw-transform payload");
    const auto visible_frame = renderer.bounds(scene, {}, {});
    const auto selected_frame = renderer.bounds(scene, {map.entity}, {});
    check(visible_frame && visible_frame->maximum == Double3{1, 1, 2} && selected_frame &&
              selected_frame->minimum == Double3{-1, -1, 2} &&
              selected_frame->maximum == Double3{101, 101, 2},
          "Tile framing lost visibility policy or explicit hidden-layer selection");
    scene.tile_maps[0].cells = map.cells;
    scene.tile_maps[0].world.m[3] = camera_world.m[3] = 1e12;
    settle();
    check(render() == baseline, "Tile camera-relative placement lost far-world cells");
    check(renderer.pick(scene, pick_view(), 16, 48) == map.entity,
          "Far Tile Map picking differs from rendered cells");
    scene.tile_maps[0].world.m[3] = camera_world.m[3] = 0;
    std::vector<TileMapCellEdit> shifted;
    for (const auto& cell : initial) {
        shifted.push_back({cell.layer, cell.address, {}});
        shifted.push_back(
            {cell.layer, {cell.address.x + 1048576, cell.address.y - 1048576}, cell.value});
    }
    scene.tile_maps[0].cells =
        std::make_shared<const TileMapData>(map.cells->with_cells(shifted, *definitions));
    camera_world.m[3] = 1048576;
    camera_world.m[7] = -1048576;
    settle();
    const auto sparse = renderer.bounds();
    check(sparse && sparse->minimum == Double3{1048575, -1048577, 2} &&
              sparse->maximum == Double3{1048577, -1048575, 2} && render() == baseline,
          "Sparse Tile Map bounds or off-origin cells were replaced by a prototype Quad");
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::White);
    host->preview_material({material}, surface);
    settle();
    check(!renderer.diagnostics().empty() && render() == baseline,
          "Incompatible image extent replaced part of the complete Tile Map draw");
    surface.textures["baseColorTexture"] = engine_texture(EngineTexture::Checker);
    host->preview_material({material}, surface);
    settle();
    check(renderer.diagnostics().empty() && render() == baseline,
          "Tile image groups could not recover their admitted frame layouts");
    scene.tile_maps[0].renderer.tint_r = std::numeric_limits<float>::quiet_NaN();
    settle();
    check(!renderer.diagnostics().empty() && render() == baseline,
          "Invalid map tint partially replaced the complete draw");
    scene.tile_maps[0].renderer.tint_r = 1;
    settle();
    check(renderer.diagnostics().empty() && render() == baseline,
          "Tile draw did not recover from invalid numeric values");
    scene.tile_maps[0].world.m[11] = -2;
    settle();
    check(render() == masked && renderer.draw_stats().instances == 0,
          "Tile chunk culling rendered cells behind the camera");
    scene.tile_maps[0].world.m[11] = 2;
    settle();
    check(render() == baseline, "Tile movement did not reach the reused image groups");
    camera_world = {};
    const std::array<TileMapCellEdit, 1> ink_cell{{{1, {0, 0}, {9, 0}}}};
    const std::array<TileMapCellEdit, 1> black_cell{{{1, {0, 0}, {tile_key, 0}}}};
    auto left = map, right = map;
    left.cells = std::make_shared<const TileMapData>(cells(map_id, set_id, ink_cell));
    right.cells = std::make_shared<const TileMapData>(cells(map_id, set_id, black_cell));
    right.entity = EntityId::generate();
    left.world.m[3] = -1;
    left.world.m[7] = right.world.m[7] = -.5;
    scene.tile_maps = {left, right};
    settle();
    const auto independent = render();
    check(independent[32 * 64 + 16] == std::array<std::uint8_t, 4>{255, 0, 255, 255} &&
              independent[32 * 64 + 48] == std::array<std::uint8_t, 4>{0, 0, 0, 255} &&
              independent[8 * 64 + 16] == std::array<std::uint8_t, 4>{32, 64, 128, 255} &&
              renderer.bundle_count() == 1 && renderer.draw_stats().instances == 2 &&
              renderer.draw_stats().calls == 1,
          "Independent actors sharing a Map asset aliased their cells or image draw values");
    check(renderer.pick(scene, pick_view(), 16, 32) == left.entity &&
              renderer.pick(scene, pick_view(), 48, 32) == right.entity &&
              !renderer.pick(scene, pick_view(), 16, 8),
          "Sparse grid selection picked empty space or another actor's cells");
    scene.tile_maps[1].world = left.world;
    scene.tile_maps[1].renderer.order = INT32_MIN;
    settle();
    check(renderer.pick(scene, pick_view(), 16, 32) == left.entity,
          "Tile selection overflowed the combined renderer/layer order");
    scene.tile_maps[0].selectable = false;
    check(renderer.pick(scene, pick_view(), 16, 32) == right.entity,
          "Tile selection lost the selectable actor beneath an overlapping map");
    scene.tile_maps[0].selectable = true;
    scene.tile_maps[1].world = right.world;
    scene.tile_maps[1].renderer.order = right.renderer.order;
    scene.tile_maps[1].cells =
        std::make_shared<const TileMapData>(right.cells->with_cells(ink_cell, *definitions));
    settle();
    const auto edited_instance = render();
    check(edited_instance[32 * 64 + 16] == independent[32 * 64 + 16] &&
              edited_instance[32 * 64 + 48] == std::array<std::uint8_t, 4>{255, 0, 255, 255} &&
              left.cells->cell(1, {}).tile == 9 && right.cells->cell(1, {}).tile == tile_key,
          "A rendered instance edit changed another actor or its previous immutable snapshot");
    std::ofstream output(directory / "cells.ppm", std::ios::binary);
    output << "P6\n64 64\n255\n";
    for (const auto& pixel : baseline)
        output.write(reinterpret_cast<const char*>(pixel.data()), 3);
    static_assert(sizeof(typename decltype(baseline)::value_type) == 4);
    save_view_png(directory / "cells.png", 64, 64,
                  {reinterpret_cast<const std::uint8_t*>(baseline.data()), baseline.size() * 4});
    check(bool(output.flush()) && renderer.diagnostics().empty(),
          "Tile draw evidence or final diagnostics failed");

    // The admitted maximum must be an actual draw, not just a source-size test.
    // This also guards against exhausting fixed backend DISCARD upload heaps.
    std::vector<TileMapCellEdit> dense;
    dense.reserve(tile_map_cell_limit);
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x)
            dense.push_back({1, {x, y}, {9, 0}});
    scene.tile_maps = {map};
    scene.tile_maps[0].cells = std::make_shared<const TileMapData>(cells(map_id, set_id, dense));
    camera_world.m[3] = camera_world.m[7] = 256;
    settings.orthographic_height = 512;
    settle();
    for (unsigned frame = 0; frame < 3; ++frame) {
        if (frame == 2) {
            const std::array<TileMapCellEdit, 1> edit{{{1, {255, 255}, {9, 1}}}};
            scene.tile_maps[0].cells = std::make_shared<const TileMapData>(
                scene.tile_maps[0].cells->with_cells(edit, *definitions));
            settle();
        }
        const auto pixels = render();
        check(renderer.draw_stats().instances == tile_map_cell_limit &&
                  renderer.draw_stats().calls == tile_map_cell_limit / MeshDraw::instance_limit &&
                  renderer.bundle_count() == 1 && renderer.diagnostics().empty() &&
                  std::all_of(pixels.begin(), pixels.end(),
                              [](const auto& pixel) {
                                  return pixel == std::array<std::uint8_t, 4>{255, 0, 255, 255};
                              }),
              "Maximum Tile Map draw lost cells, pixels or upload capacity across frames/edit");
    }
}
} // namespace forge::test_tile_map_render
