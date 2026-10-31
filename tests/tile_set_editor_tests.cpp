#include "asset_bytes.hpp"
#include "editor/tile_set_editor.hpp"
#include "imgui_software_capture.hpp"
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
    double preview_seconds = 0;
    editor.draw_preview = [&](const auto& source, std::uint64_t revision, const auto& catalog, auto,
                              double seconds) {
        check(revision && catalog && source.asset() == editor.document()->source().asset(),
              "Tile Set preview lost document identity");
        check(std::isfinite(seconds) && seconds >= 0, "Tile Set asset preview time is invalid");
        ++previews;
        preview_revision = revision;
        preview_seconds = seconds;
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
    const auto before_collision = editor.document()->source().document;
    const auto collision_revision = editor.document()->revision();
    click("tile_set:collision_kind");
    click("tile_set:collision_kind:2");
    check(editor.document()->revision() == collision_revision && !editor.can_undo(),
          "Unapplied polygon changed source or allowed history traversal");
    frame();
    check(!test::ui_targets.at("tile_set:frame").enabled,
          "Unapplied polygon could be discarded by an unrelated tile field edit");
    context.selection.clear();
    frame();
    click("tile_set:collision_review");
    check(context.selection.member() == "tile/4",
          "Collision draft could not be reopened after another selection");
    click("tile_set:collision_add");
    click("tile_set:collision_apply");
    check(editor.document()->revision() == collision_revision && !editor.diagnostic().empty(),
          "Collinear draft polygon entered source/history");
    number("tile_set:collision_point:3:x", "0");
    number("tile_set:collision_point:3:y", "1");
    click("tile_set:collision_apply");
    check(editor.document()->source().document.at("version") == 2 &&
              editor.document()->source().values().tiles.back().collision.points.size() == 4 &&
              editor.can_undo(),
          "Polygon Inspector coordinates/apply did not promote and retain history");
    click("tile_set:undo");
    check(editor.document()->source().document == before_collision,
          "Polygon Undo did not restore the complete legacy source");
    click("tile_set:redo");
    check(editor.document()->source().values().tiles.back().collision.points.size() == 4,
          "Polygon Redo lost authored vertices");
    click("tile_set:collision_kind");
    click("tile_set:collision_kind:1");
    click("tile_set:collision_discard");
    check(editor.document()->source().values().tiles.back().collision.kind ==
              TileCollisionKind::Convex,
          "Discard changed the applied collision profile");
    const auto convex_source = editor.document()->source().document;
    click("tile_set:collision_kind");
    click("tile_set:collision_kind:3");
    click("tile_set:collision_add");
    click("tile_set:collision_add");
    number("tile_set:collision_point:2:y", "0.5");
    number("tile_set:collision_point:3:x", "0.5");
    number("tile_set:collision_point:3:y", "0.5");
    number("tile_set:collision_point:4:x", "0.5");
    number("tile_set:collision_point:4:y", "1");
    number("tile_set:collision_point:5:x", "0");
    number("tile_set:collision_point:5:y", "1");
    click("tile_set:collision_apply");
    const auto concave_source = editor.document()->source().document;
    check(concave_source.at("version") == 3 &&
              editor.document()->source().values().tiles.back().collision.kind ==
                  TileCollisionKind::Polygon &&
              editor.document()->source().values().tiles.back().collision.points.size() == 6,
          "Concave Inspector draft did not promote/apply the complete six-point outline");
    click("tile_set:undo");
    check(editor.document()->source().document == convex_source,
          "Concave Inspector Undo changed the previous convex source");
    click("tile_set:redo");
    check(editor.document()->source().document == concave_source,
          "Concave Inspector Redo lost the promoted outline");
    const auto animation_revision = editor.document()->revision();
    click("tile_set:animation_enabled");
    frame();
    check(editor.has_unapplied_edits() && !editor.can_undo() &&
              editor.document()->revision() == animation_revision &&
              !test::ui_targets.at("tile_set:save").enabled &&
              !test::ui_targets.at("tile_set:frame").enabled &&
              !test::ui_targets.at("tile_set:collision_kind").enabled,
          "Animation draft changed source or allowed conflicting edits/history/save");
    context.selection.clear();
    frame();
    click("tile_set:animation_review");
    check(context.selection.member() == "tile/4",
          "Animation draft could not be reopened after another selection");
    number("tile_set:animation_duration:0", "0");
    click("tile_set:animation_apply");
    check(editor.has_unapplied_edits() && !editor.diagnostic().empty() &&
              editor.document()->revision() == animation_revision &&
              editor.document()->source().document == concave_source,
          "Invalid animation duration entered source/history");
    number("tile_set:animation_duration:0", "0.125");
    click("tile_set:animation_add");
    number("tile_set:animation_frame:1", "1");
    number("tile_set:animation_duration:1", "0.25");
    click("tile_set:animation_apply");
    const auto animated_source = editor.document()->source().document;
    const auto animation = editor.document()->source().values().tiles.back().animation;
    check(!editor.has_unapplied_edits() && animated_source.at("version") == 4 && animation &&
              animation->enabled && animation->frames.size() == 2 &&
              animation->frames[0] == TileAnimationFrame{3, .125} &&
              animation->frames[1] == TileAnimationFrame{1, .25} &&
              editor.document()->source().values().tiles.back().collision.kind ==
                  TileCollisionKind::Polygon,
          "Animation Inspector did not apply the sequence while retaining collision");
    click("tile_set:animation_remove");
    click("tile_set:animation_discard");
    check(!editor.has_unapplied_edits() && editor.document()->source().document == animated_source,
          "Animation discard changed the applied source");
    const auto before_preview = editor.document()->revision();
    click("tile_set:preview_play");
    for (unsigned i = 0; i < 4; ++i)
        frame();
    check(preview_seconds > 0, "Asset animation preview did not advance");
    click("tile_set:preview_play");
    const auto paused_preview = preview_seconds;
    frame();
    frame();
    check(preview_seconds == paused_preview, "Paused asset preview advanced");
    click("tile_set:preview_restart");
    check(preview_seconds == 0 && editor.document()->revision() == before_preview &&
              editor.document()->source().document == animated_source,
          "Asset preview restart changed authored source or did not reset local time");
    click("tile_set:autotile_enabled");
    check(editor.has_unapplied_edits() && !editor.can_undo() && !editor.can_redo(),
          "Unapplied neighbor rules did not lock source history");
    click("tile_set:autotile_neighbor:0:0");
    click("tile_set:autotile_neighbor:0:7");
    click("tile_set:autotile_neighbor:0:7");
    click("tile_set:autotile_add");
    click("tile_set:autotile_target:1");
    click("tile_set:autotile_target:1:2");
    click("tile_set:autotile_up:1");
    click("tile_set:autotile_apply");
    const auto neighbor_source = editor.document()->source().document;
    const auto neighbor = editor.document()->source().values().tiles.back().autotile;
    check(
        !editor.has_unapplied_edits() && neighbor_source.at("version") == 5 && neighbor &&
            neighbor->enabled && neighbor->rules.size() == 2 &&
            neighbor->rules[0] == TileNeighborRule{0, 0, 2} &&
            neighbor->rules[1] == TileNeighborRule{129, 1, 4} &&
            neighbor_source["tiles"].back()["animation"] ==
                animated_source["tiles"].back()["animation"] &&
            neighbor_source["tiles"].back()["collision"] ==
                animated_source["tiles"].back()["collision"],
        "Neighbor Inspector did not preserve rule order, compass conditions and sibling profiles");
    click("tile_set:autotile_remove:0");
    check(editor.has_unapplied_edits(), "Remove neighbor rule did not stage an edit at scale " +
                                            std::to_string(scale) + ": " +
                                            editor.document()->source().document.dump());
    click("tile_set:autotile_discard");
    check(!editor.has_unapplied_edits() && editor.document()->source().document == neighbor_source,
          "Discarding neighbor edits changed authored source");
    click("tile_set:undo");
    check(editor.document()->source().document == animated_source,
          "Neighbor Undo lost source history");
    click("tile_set:redo");
    check(editor.document()->source().document == neighbor_source,
          "Neighbor Redo lost source history");
    click("tile_set:undo");
    click("tile_set:undo");
    check(editor.document()->source().document == concave_source,
          "Animation Undo lost the complete previous source/profile version");
    click("tile_set:redo");
    check(editor.document()->source().document == animated_source,
          "Animation Redo lost the sequence");
    click("tile_set:undo");
    click("tile_set:undo");
    click("tile_set:undo");
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
void capture_controls(const std::filesystem::path& root, bool concave = false,
                      bool animated = false, bool neighbors = false) {
    std::filesystem::create_directories(root / "project");
    test::ImGuiSoftwareCapture capture(760, neighbors ? 620 : concave ? 1100 : 1000);
    ui::style(1);
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root / "project", true);
    auto document =
        TileSetDocument::create(project.writer_guard(), "Assets/Collision.tileset.json");
    document->edit_source(document->revision(), "Capture tile profile", [=](auto& source) {
        source.document["next_id"] = 3;
        source.document["images"] = {{{"id", 1},
                                      {"name", "Tile image"},
                                      {"material", AssetId::generate()},
                                      {"grid",
                                       {{"columns", animated ? 4 : 1},
                                        {"rows", 1},
                                        {"margin_x", 0},
                                        {"margin_y", 0},
                                        {"spacing_x", 0},
                                        {"spacing_y", 0}}}}};
        source.document["tiles"] = {{{"id", 2},
                                     {"name", animated  ? "Forward arrows"
                                              : concave ? "Concave corner"
                                                        : "Convex ramp"},
                                     {"image", 1},
                                     {"frame", 0}}};
        if (animated)
            source.set_animation(2, {{"enabled", true},
                                     {"frames",
                                      {{{"frame", 0}, {"seconds", .2}},
                                       {{"frame", 1}, {"seconds", .2}},
                                       {{"frame", 2}, {"seconds", .2}},
                                       {{"frame", 3}, {"seconds", .2}}}}});
        else if (concave)
            source.set_collision(
                2, {{"kind", "polygon"},
                    {"points", {{0., 0.}, {1., 0.}, {1., .5}, {.5, .5}, {.5, 1.}, {0., 1.}}}});
        else
            source.set_collision(2,
                                 {{"kind", "convex"}, {"points", {{0., 0.}, {1., 0.}, {1., 1.}}}});
        if (neighbors) {
            source.document["next_id"] = 5;
            source.document["tiles"][0]["name"] = "Paint this tile";
            source.document["tiles"].push_back(
                {{"id", 3}, {"name", "North edge"}, {"image", 1}, {"frame", 0}});
            source.document["tiles"].push_back(
                {{"id", 4}, {"name", "South edge"}, {"image", 1}, {"frame", 0}});
            source.set_autotile(2, {{"enabled", true},
                                    {"rules",
                                     {{{"mask", 1}, {"same", 0}, {"tile", 3}},
                                      {{"mask", 16}, {"same", 0}, {"tile", 4}}}}});
        }
    });
    document->save();
    document.reset();
    TileSetEditor editor("unused-capture-worker");
    editor.open(project, "Assets/Collision.tileset.json");
    check(editor.document(), "Collision capture source did not open");
    for (unsigned frame = 0; frame < 3; ++frame) {
        capture.new_frame();
        ImGui::SetNextWindowPos({10, 10});
        ImGui::SetNextWindowSize({740, neighbors ? 600.f : concave ? 1080.f : 980.f});
        ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        editor.inspect("tile/2", false);
        if (neighbors)
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
        ImGui::End();
        capture.render();
    }
    check(editor.diagnostic().empty(), "Collision capture has source diagnostics");
    capture.save(root / (neighbors  ? "neighbor-tile-controls.png"
                         : animated ? "animated-tile-controls.png"
                         : concave  ? "concave-tile-collision-controls.png"
                                    : "tile-collision-profile.png"));
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && (std::string_view(argv[2]) == "--capture" ||
                          std::string_view(argv[2]) == "--capture-concave" ||
                          std::string_view(argv[2]) == "--capture-animation" ||
                          std::string_view(argv[2]) == "--capture-neighbors")) {
            capture_controls(std::filesystem::absolute(argv[1]),
                             std::string_view(argv[2]) == "--capture-concave",
                             std::string_view(argv[2]) == "--capture-animation",
                             std::string_view(argv[2]) == "--capture-neighbors");
            return 0;
        }
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
