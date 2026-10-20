#pragma once
#include "editor/tile_map_preview.hpp"
#include "sprite_draw_render_tests.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_document.hpp"
#include "tile_set_document.hpp"
#include "texture_authoring.hpp"
#include <imgui_internal.h>
#include <thread>

namespace forge::test_tile_canvas {
inline void exercise(DiligentPresentation& presentation, Diligent::IDeviceContext* context,
                     const std::filesystem::path& worker, const std::filesystem::path& root) {
    using test_tile_asset_io::check;
    std::filesystem::create_directories(root / "Assets");
    auto lease = std::make_shared<ProjectLease>(root);
    const auto material = test_tile_asset_io::material(root, lease);
    auto set = TileSetDocument::create(lease, "Assets/Sheet.tileset.json");
    set->edit(set->revision(), "Original sheet", [&](auto& source) {
        source["next_id"] = 4;
        source["images"] = {{{"id", 1},
                             {"name", "Checker sheet"},
                             {"material", material},
                             {"grid",
                              {{"columns", 4},
                               {"rows", 4},
                               {"margin_x", 0},
                               {"margin_y", 0},
                               {"spacing_x", 0},
                               {"spacing_y", 0}}}}};
        source["tiles"] = {{{"id", 2}, {"name", "Ink"}, {"image", 1}, {"frame", 0}},
                           {{"id", 3}, {"name", "Black"}, {"image", 1}, {"frame", 1}}};
    });
    set->save();
    AssetImportService importer(lease, tile_asset_import_registry(worker),
                                desktop_texture_target());
    auto receipt =
        test_tile_asset_io::publish(importer, set->locator(), set->source().asset(), false);
    check(receipt.published, "Canvas Set publication failed: " + receipt.diagnostic);
    auto map = TileMapDocument::create(lease, "Assets/Map.tilemap.json", [&](AssetId id) {
        return TileMapSource{TileMapValue::create(id, set->source().asset())};
    });
    const std::array<TileMapCellEdit, 2> cells{{{1, {0, 0}, {3, 0}}, {1, {1, 0}, {2, 0}}}};
    map->edit_source(map->revision(), "Paint markers",
                     [&](auto& source) { source.document = source.document.paint(cells); });
    map->save();
    const auto baseline =
        asset_detail::read_bytes(root / map->locator(), tile_map_source_byte_limit);
    auto values = std::make_shared<const TileMapData>(
        decode_cooked_tile_map(cook_tile_map(map->source().document), map->source().asset()));
    auto catalog = std::make_shared<const AssetCatalog>(AssetCatalog::open_project(root));
    ResourcePool<TileSetAsset> sets;
    const auto ticket = request_tile_set(sets, root, catalog, {set->source().asset()});
    check(sets.wait(ticket, std::chrono::seconds(10)), "Canvas definitions did not load");
    const auto definitions_lease = sets.acquire(ticket);
    auto definitions = std::make_shared<const TileSetData>(definitions_lease.get());
    TileMapPreview canvas(presentation, context, root, values->asset(), values->tile_set());
    Diligent::ITextureView* image = nullptr;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    do {
        presentation.begin_frame();
        image = canvas.render(values, definitions, catalog, 64, 64);
        presentation.end_frame(context);
        context->Flush();
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
        if (!canvas.pending())
            break;
        check(std::chrono::steady_clock::now() < deadline, "Canvas render did not settle");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (true);
    check(image && canvas.diagnostics().empty(), "Canvas render has diagnostics");
    const auto pixels =
        test_sprite_draw::read_pixels(presentation.device(), context, image->GetTexture());
    check(pixels[32 * 64 + 19] == std::array<std::uint8_t, 4>{0, 0, 0, 255},
          "Canvas mirrored the left black marker");
    const auto ink = pixels[32 * 64 + 45];
    check(ink[0] > 128 && ink[2] > 128 && ink[1] < std::min(ink[0], ink[2]) / 2 && ink[3] == 255,
          "Canvas lost the right magenta marker");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 640};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* font;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&font, &width, &height);
    ui::style(1.5f);
    test::observe_ui = true;
    std::optional<TileCellAddress> hover;
    bool stroke = false, painting = true;
    const auto frame = [&] {
        presentation.begin_frame();
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        ImGui::Begin("Tile canvas");
        hover = canvas.draw(values, definitions, catalog, stroke, painting);
        ImGui::End();
        ImGui::Render();
        presentation.end_frame(context);
        context->Flush();
        context->FinishFrame();
        presentation.device()->ReleaseStaleResources();
    };
    frame();
    frame();
    const auto move = [&](float fraction) {
        const auto box = test::ui_targets.at("tile_map:preview_image");
        io.AddMousePosEvent(box.minimum.x + (box.maximum.x - box.minimum.x) * fraction,
                            (box.minimum.y + box.maximum.y) * .5f);
        frame();
    };
    move(.4f);
    check(hover && hover->x == 0 && hover->y == 0, "Canvas pointer did not select the left marker");
    move(.6f);
    check(hover && hover->x == 1 && hover->y == 0,
          "Canvas pointer did not select the right marker");
    move(.9f);
    check(hover.has_value(), "Canvas empty grid is not paintable");
    const auto before = *hover;
    io.AddMouseWheelEvent(0, 6);
    frame();
    frame();
    check(hover && hover->x < before.x, "Canvas wheel did not zoom toward its center");
    stroke = true;
    const auto captured = *hover;
    io.AddMouseButtonEvent(0, true);
    io.AddMouseWheelEvent(0, -12);
    frame();
    frame();
    check(hover == captured, "Captured paint gesture allowed wheel navigation");
    io.AddMouseButtonEvent(0, false);
    stroke = false;
    frame();
    move(.5f);
    check(hover.has_value(), "Canvas center is unavailable before pan");
    const auto pan_cell = *hover;
    io.AddMouseButtonEvent(2, true);
    frame();
    const auto start = io.MousePos;
    io.AddMousePosEvent(start.x + 150, start.y);
    frame();
    frame();
    check(!hover, "Middle-mouse navigation also offered a paint sample");
    io.AddMouseButtonEvent(2, false);
    frame();
    move(.5f);
    check(hover && hover->x < pan_cell.x, "Canvas pan moved in the wrong direction");
    painting = false;
    frame();
    check(!hover, "Read-only canvas offered a paint sample");
    check(asset_detail::read_bytes(root / map->locator(), tile_map_source_byte_limit) == baseline &&
              map->source().document.occupied() == 2,
          "Canvas navigation modified authored cells");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace forge::test_tile_canvas
