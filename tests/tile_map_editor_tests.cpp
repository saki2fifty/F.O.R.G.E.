#include "asset_bytes.hpp"
#include "editor/tile_map_editor.hpp"
#include "model_placement.hpp"
#include "tile_asset_fixture.hpp"
#include "tile_set_document.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <thread>
using namespace forge;
using test_tile_asset_io::check;
namespace {
void exercise(const std::filesystem::path& scratch, const std::filesystem::path& worker,
              float scale, bool compact) {
    std::cout << "scale=" << scale << " compact=" << compact << std::endl;
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const auto material = test_tile_asset_io::material(root, project.writer_guard());
    auto set = TileSetDocument::create(project.writer_guard(), "Assets/Sheet.tileset.json");
    set->edit(set->revision(), "Definitions", [&](auto& source) {
        source["next_id"] = 4;
        source["images"] = {{{"id", 1},
                             {"name", "Sheet"},
                             {"material", material},
                             {"grid",
                              {{"columns", 2},
                               {"rows", 1},
                               {"margin_x", 0},
                               {"margin_y", 0},
                               {"spacing_x", 0},
                               {"spacing_y", 0}}}}};
        source["tiles"] = {{{"id", 2}, {"name", "Ink"}, {"image", 1}, {"frame", 0}},
                           {{"id", 3}, {"name", "Black"}, {"image", 1}, {"frame", 1}}};
    });
    set->save();
    AssetImportService importer(project.writer_guard(), tile_asset_import_registry(worker),
                                desktop_texture_target());
    const auto receipt =
        test_tile_asset_io::publish(importer, set->locator(), set->source().asset(), false);
    check(receipt.published, "Fixture Tile Set publication: " + receipt.diagnostic);
    auto map =
        TileMapDocument::create(project.writer_guard(), "Assets/Map.tilemap.json", [&](AssetId id) {
            return TileMapSource{TileMapValue::create(id, set->source().asset())};
        });
    const auto original = map->source();
    map.reset();
    set.reset();
    const auto disk =
        asset_detail::read_bytes(root / "Assets/Map.tilemap.json", tile_map_source_byte_limit);
    const auto scene_before = scene.document();
    TileMapEditor editor(worker);
    editor.open(project, "Assets/Map.tilemap.json");
    check(editor.document(), "Tile Map did not open");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = compact ? ImVec2{1100, 1000} : ImVec2{1500, 1200};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    std::shared_ptr<const TileMapData> preview;
    std::shared_ptr<const TileSetData> definitions;
    unsigned releases = 0;
    editor.release_preview = [&] { ++releases; };
    editor.draw_preview = [&](auto cells, auto tiles, auto catalog, bool,
                              bool enabled) -> std::optional<TileCellAddress> {
        check(cells && catalog && cells->asset() == editor.document()->source().asset(),
              "Preview lost map identity");
        preview = std::move(cells);
        definitions = std::move(tiles);
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("Canvas", {std::max(1.f, ImGui::GetContentRegionAvail().x), 200});
        FORGE_UI_PROBE("tile_map:test_canvas");
        if (!enabled || !ImGui::IsItemHovered())
            return {};
        return TileCellAddress{std::int32_t(std::floor((io.MousePos.x - origin.x - 100) / 20)),
                               std::int32_t(std::floor((origin.y + 100 - io.MousePos.y) / 20))};
    };
    bool locked = false;
    std::string message;
    const auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        const float center = io.DisplaySize.x * .6f;
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({center, io.DisplaySize.y - 20}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::SetNextWindowPos({center, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x - center, io.DisplaySize.y - 20},
                                 ImGuiCond_Always);
        ImGui::Begin("Inspector");
        if (context.selection.document() == "tile_map")
            editor.inspect(context.selection.member(), locked);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!definitions && std::chrono::steady_clock::now() < end) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(bool(definitions), "Published Tile Set did not reach palette: " + editor.diagnostic());
    const auto point = [&](const std::string& id) {
        frame();
        for (unsigned tries = 0; tries < 100; ++tries) {
            check(test::ui_targets.contains(id), "Map control missing: " + id);
            const auto box = test::ui_targets.at(id);
            check(box.enabled, "Map control disabled: " + id);
            if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                    (box.minimum.y + box.maximum.y) * .5f);
                frame();
                return;
            }
            io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                (box.clip_minimum.y + box.clip_maximum.y) * .5f);
            frame();
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 1.f : -1.f);
            frame();
        }
        throw std::runtime_error("Map control unreachable: " + id);
    };
    const auto click = [&](const std::string& id) {
        point(id);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    const auto number = [&](const std::string& id, const char* text) {
        click(id);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame();
        io.AddInputCharactersUTF8(text);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    const auto cell = [&](int x, int y) {
        if (!editor.stroke_active())
            point("tile_map:test_canvas");
        const auto box = test::ui_targets.at("tile_map:test_canvas");
        io.AddMousePosEvent(box.minimum.x + 100 + float(x) * 20 + 10,
                            box.minimum.y + 100 - float(y) * 20 - 10);
        frame();
    };
    cell(-2, 0);
    io.AddMouseButtonEvent(0, true);
    frame();
    check(editor.stroke_active() && editor.document()->source().document.occupied() == 0,
          "Mouse-down changed authored cells before release");
    cell(2, 0);
    frame();
    check(preview->occupied() == 5, "Drag skipped cells across a negative seam");
    io.AddMouseButtonEvent(0, false);
    frame();
    check(!editor.stroke_active() && editor.document()->source().document.occupied() == 5,
          "Release did not commit the complete stroke");
    for (int x = -2; x <= 2; ++x)
        check(editor.document()->source().document.cell(1, {x, 0}).tile == 2,
              "Stroke status moved the canvas beneath the held pointer");
    click("tile_map:undo");
    check(editor.document()->source().document.occupied() == 0,
          "One Undo did not remove whole stroke");
    click("tile_map:redo");
    check(editor.document()->source().document.occupied() == 5, "Redo lost stroke");
    cell(0, 1);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    check(editor.document()->source().document.occupied() == 5 && preview->occupied() == 5,
          "Escape retained staged cells");
    cell(0, 1);
    io.AddMouseButtonEvent(0, true);
    frame();
    check(editor.stroke_active(), "Focus-loss fixture did not begin a stroke");
    io.AddFocusEvent(false);
    frame();
    io.AddFocusEvent(true);
    io.AddMouseButtonEvent(0, false);
    frame();
    frame();
    check(!editor.stroke_active() && editor.document()->source().document.occupied() == 5 &&
              preview->occupied() == 5,
          "Application focus loss committed or retained staged cells");
    click("tile_map:add_layer");
    check(editor.document()->source().document.layers().size() == 2, "Layer creation failed");
    context.selection.select_document_item("tile_map", "layer");
    frame();
    number("tile_map:layer_order", "-7");
    check(editor.document()->source().document.layers().back().order == -7,
          "Layer Inspector edit failed");
    cell(0, 0);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    check(editor.document()->source().document.cell(2, {0, 0}).tile == 2,
          "Paint targeted the wrong layer");
    locked = true;
    frame();
    check(!test::ui_targets.at("tile_map:save").enabled, "Locked source remained editable");
    locked = false;
    frame();
    check(scene.document() == scene_before &&
              asset_detail::read_bytes(root / "Assets/Map.tilemap.json",
                                       tile_map_source_byte_limit) == disk,
          "Draft paint or Inspector changed Scene/source disk before Save");
    editor.request_close();
    frame();
    frame();
    click("tile_map:close_cancel");
    check(editor.document() && editor.close_cancelled, "Close cancel discarded draft");
    editor.request_save();
    frame();
    const auto publish_end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (editor.pending() && std::chrono::steady_clock::now() < publish_end) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(!editor.pending() && !editor.dirty() && editor.take_catalog(),
          "Map Save / Publish failed: " + editor.diagnostic());
    const auto placed = asset_detail::instantiate_tile_map(scene, AssetCatalog::open_project(root),
                                                           {editor.document()->source().asset()},
                                                           {2, -3, 4}, "Painted Map");
    check(scene.entity(placed.str()).get<TileMapRenderer>().map.id ==
                  editor.document()->source().asset() &&
              scene.undo() && scene.document() == scene_before,
          "Published map placement lost its reference or atomic Scene Undo");
    editor.request_close();
    frame();
    check(!editor.document() && releases == 1, "Published close retained source");
    // A broken Set must still allow bounded erase, Undo and close/discard.
    auto broken_catalog = AssetCatalog::open_project(root);
    std::vector<AssetRecord> surviving;
    for (const auto& [id, record] : broken_catalog.records())
        if (id != original.document.tile_set())
            surviving.push_back(record);
    broken_catalog.replace_all(std::move(surviving));
    broken_catalog.save(AssetCatalog::project_index(root));
    editor.open(project, "Assets/Map.tilemap.json");
    frame();
    check(!definitions && editor.document()->source().document.occupied() == 6,
          "Broken Set retained stale palette");
    click("tile_map:tool");
    click("tile_map:tool:2");
    cell(0, 0);
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    check(editor.document()->source().document.occupied() == 5,
          "Erase required unavailable Tile Set");
    click("tile_map:undo");
    check(editor.document()->source().document.occupied() == 6,
          "Missing-Set erase Undo lost cells");
    click("tile_map:redo");
    editor.request_close();
    frame();
    frame();
    click("tile_map:close_discard");
    check(!editor.document() && context.selection.document().empty(),
          "Discard retained document selection");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 3, "Expected worker and scratch directory");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(argv[2], argv[1], scale, compact);
        std::cout << "Tile Map ImGui paint/history/cancel/layers/Inspector/publication/repair "
                     "passed at three scales\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
