#include "asset_bytes.hpp"
#include "editor/tile_set_editor.hpp"
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
void exercise(const std::filesystem::path& scratch, float scale, bool compact) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    auto created = TileSetDocument::create(project.writer_guard(), "Assets/Original.tileset.json");
    created->edit(created->revision(), "Fixture", [](auto& source) {
        source["next_id"] = 4;
        source["opaque"] = "preserved";
        source["images"] = {{{"id", 1},
                             {"name", "Sheet"},
                             {"material", AssetId::generate()},
                             {"grid",
                              {{"columns", 2},
                               {"rows", 2},
                               {"margin_x", 0},
                               {"margin_y", 0},
                               {"spacing_x", 0},
                               {"spacing_y", 0}}}}};
        source["tiles"] = {{{"id", 2}, {"name", "Ink"}, {"image", 1}, {"frame", 0}},
                           {{"id", 3}, {"name", "Black"}, {"image", 1}, {"frame", 1}}};
    });
    created->save();
    const auto original = created->source().document;
    created.reset();
    const auto disk =
        asset_detail::read_bytes(root / "Assets/Original.tileset.json", tile_set_source_byte_limit);
    const auto scene_before = scene.document();
    TileSetEditor editor("unused-import-worker");
    editor.open(project, "Assets/Original.tileset.json");
    check(editor.document(), "Tile Set source did not open");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = compact ? ImVec2{1100, 800} : ImVec2{1500, 1100};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    unsigned previews = 0, releases = 0;
    std::uint64_t preview_revision = 0;
    editor.draw_preview = [&](const auto& source, std::uint64_t revision, const auto& catalog,
                              auto) {
        check(revision && catalog && source.asset() == editor.document()->source().asset(),
              "Tile Set preview lost document identity");
        ++previews;
        preview_revision = revision;
        ImGui::Dummy({100, 30});
    };
    editor.release_preview = [&] { ++releases; };
    bool locked = false;
    std::string message;
    const auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        const float center_width = io.DisplaySize.x * .6f;
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({center_width, io.DisplaySize.y - 20}, ImGuiCond_Always);
        editor.draw(project, locked);
        ImGui::SetNextWindowPos({center_width, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x - center_width, io.DisplaySize.y - 20},
                                 ImGuiCond_Always);
        ImGui::Begin("Inspector");
        if (context.selection.document() == "tile_set")
            editor.inspect(context.selection.member(), locked);
        ImGui::End();
        ImGui::Render();
    };
    frame();
    frame();
    const auto point = [&](const std::string& id) {
        frame();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            check(test::ui_targets.contains(id), "Tile Set control missing: " + id);
            const auto box = test::ui_targets.at(id);
            check(box.enabled, "Tile Set control disabled: " + id);
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
        throw std::runtime_error("Tile Set control unreachable: " + id);
    };
    const auto click = [&](const std::string& id) {
        point(id);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    const auto number = [&](const std::string& id, const char* value) {
        click(id);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
        frame();
        io.AddInputCharactersUTF8(value);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame();
    };
    click("tile_set:image:1");
    check(context.selection.document() == "tile_set" && context.selection.member() == "image/1",
          "Image selection did not use document-local Inspector selection");
    const auto revision = editor.document()->revision();
    number("tile_set:grid:columns", "0");
    check(editor.document()->revision() == revision && !editor.diagnostic().empty(),
          "Invalid grid changed the source");
    number("tile_set:grid:columns", "4");
    check(editor.document()->source().values().images[0].grid.columns == 4,
          "Inspector grid edit did not apply");
    click("tile_set:undo");
    check(editor.document()->source().document == original,
          "Grid Undo lost source or opaque values");
    click("tile_set:add_tile");
    check(editor.document()->source().values().tiles.size() == 3 &&
              context.selection.member() == "tile/4",
          "Add tile did not select its new stable key");
    number("tile_set:frame", "3");
    check(editor.document()->source().values().tiles.back().frame == 3,
          "Tile Inspector frame did not apply");
    click("tile_set:remove_tile");
    check(editor.document()->source().values().tiles.size() == 2, "Remove tile did not apply");
    frame(); // Inspector reconciles the source changed by the preceding frame's button.
    check(context.selection.document().empty(), "Removed tile retained a stale Inspector key");
    click("tile_set:undo");
    check(editor.document()->source().values().tiles.back().id == 4 &&
              editor.document()->source().values().tiles.back().frame == 3,
          "Tile removal Undo changed local identity or frame");
    click("tile_set:tile:4");
    locked = true;
    frame();
    check(!test::ui_targets.at("tile_set:save").enabled &&
              !test::ui_targets.at("tile_set:frame").enabled,
          "Locked source controls stayed editable");
    locked = false;
    frame();
    check(scene.document() == scene_before &&
              asset_detail::read_bytes(root / "Assets/Original.tileset.json",
                                       tile_set_source_byte_limit) == disk,
          "Draft UI wrote authored Scene/source before Save");
    editor.request_close();
    frame();
    frame();
    click("tile_set:close_cancel");
    check(editor.document() && editor.close_cancelled, "Close cancel discarded a Tile Set draft");
    editor.request_close();
    frame();
    frame();
    click("tile_set:close_discard");
    check(!editor.document() && releases == 1 && previews > 0 &&
              context.selection.document().empty(),
          "Discard did not release the source preview or its Inspector selection");
    editor.open(project, "Assets/Original.tileset.json");
    frame();
    const auto before_reload = preview_revision;
    editor.request_close();
    frame();
    frame();
    click("tile_set:close_discard");
    auto external = original;
    external["tiles"][0]["name"] = "External change";
    atomic_write(root / "Assets/Original.tileset.json", external.dump(2));
    editor.open(project, "Assets/Original.tileset.json");
    frame();
    check(editor.document()->revision() == 1 && preview_revision > before_reload &&
              editor.document()->source().values().tiles.front().name == "External change",
          "Same-source reopen reused an old preview revision after external source change");
    editor.request_close();
    frame();
    frame();
    click("tile_set:close_discard");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected scratch directory");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(argv[1], scale, compact);
        std::cout << "Tile Set document/Inspector input, history and close guards passed at three "
                     "scales\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
