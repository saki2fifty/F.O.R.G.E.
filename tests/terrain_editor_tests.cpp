#include "asset_bytes.hpp"
#include "editor/terrain_editor.hpp"
#include <imgui_internal.h>
#include <iostream>
using namespace forge;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void exercise(const std::filesystem::path& scratch, float scale, bool compact) {
    const auto root = scratch / AssetId::generate().str();
    std::filesystem::create_directories(root / "Assets");
    EngineContext engine;
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root, true);
    const std::filesystem::path locator = "Assets/Original.terrain.json";
    auto created = TerrainDocument::create(project.writer_guard(), locator, [](AssetId id) {
        return TerrainSource::create(id, 5, 4);
    });
    const auto original = created->source().document;
    created.reset();
    const auto disk = asset_detail::read_bytes(root / locator, terrain_source_byte_limit);
    const auto actor =
        authoring_command(scene, "entity.create",
                          {{"name", "Terrain UI fixture"}, {"recipe", "render.mesh"}})
            .at("selected")
            .get<std::string>();
    auto expected_scene = scene.document();
    const auto original_scene = expected_scene;
    bool check_scene = true;
    TerrainEditor editor;
    editor.open(project, locator);
    require(editor.document(), "Terrain source failed to open");
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = compact ? ImVec2{780, 820} : ImVec2{1500, 1900};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    test::observe_ui = true;
    ui::EditorUiContext context;
    ui::ContextScope scope(context);
    context.selection.select_entity(actor);
    unsigned previews = 0, releases = 0;
    double displayed_height = 0;
    std::optional<MeshBrushSample> picked_override;
    TerrainTileId displayed_tile;
    editor.draw_preview = [&](const auto& source, std::uint64_t revision, const auto& catalog,
                              auto settings) -> std::optional<MeshBrushSample> {
        require(source.asset() == editor.document()->source().asset() && revision && catalog,
                "Terrain preview lost isolated source identity");
        require(!check_scene || scene.document() == expected_scene,
                "Terrain draft preview mutated the authored scene");
        ++previews;
        displayed_height = terrain_height(source.raster(), 2, 2).value_or(0);
        displayed_tile = settings.tile;
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size{std::max(1.f, ImGui::GetContentRegionAvail().x), 220 * scale};
        ImGui::InvisibleButton("Fixture preview", size);
        FORGE_UI_PROBE("terrain:preview_image");
        if (!ImGui::IsItemHovered())
            return {};
        if (picked_override)
            return picked_override;
        const auto mouse = io.MousePos;
        return MeshBrushSample{
            {(mouse.x - origin.x) * 4 / size.x, 0, (mouse.y - origin.y) * 4 / size.y}, .5};
    };
    editor.release_preview = [&] { ++releases; };
    std::string message;
    bool locked = false;
    auto frame = [&] {
        test::ui_targets.clear();
        editor.poll(project, message);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({io.DisplaySize.x - 20, io.DisplaySize.y - 20}, ImGuiCond_Always);
        editor.draw(project, scene, locked);
        ImGui::Render();
    };
    frame();
    frame();
    auto point = [&](const std::string& id) {
        frame(); // Inspect the next frame after a preceding button changed source.
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            require(test::ui_targets.contains(id), "Terrain control missing: " + id);
            const auto box = test::ui_targets.at(id);
            require(box.enabled, "Terrain control disabled: " + id);
            if (box.minimum.y >= box.clip_minimum.y && box.maximum.y <= box.clip_maximum.y) {
                io.AddMousePosEvent((box.minimum.x + box.maximum.x) * .5f,
                                    (box.minimum.y + box.maximum.y) * .5f);
                frame();
                return;
            }
            io.AddMousePosEvent(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f);
            frame();
            io.AddMouseWheelEvent(0, box.minimum.y < box.clip_minimum.y ? 1.f : -1.f);
            frame();
        }
        throw std::runtime_error("Terrain control could not be reached: " + id);
    };
    auto click = [&](const std::string& id) {
        point(id);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    };
    auto number = [&](const std::string& id, const char* value) {
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
    const auto revision = editor.document()->revision();
    number("terrain:side", "1");
    require(editor.document()->revision() == revision &&
                editor.document()->source().document == original && !editor.diagnostic().empty(),
            "Invalid resolution changed source/history");
    click("terrain:add_tile");
    require(editor.document()->source().raster().tiles.size() == 2,
            "Actual UI did not add a terrain tile");
    click("terrain:undo");
    require(editor.document()->source().document == original, "Terrain tile Undo failed");
    click("terrain:tool");
    click("terrain:tool:1");
    point("terrain:preview_image");
    const auto before = editor.document()->revision();
    io.AddMouseButtonEvent(0, true);
    frame();
    require(editor.stroke_active() && editor.document()->revision() == before,
            "Mouse press prematurely committed terrain stroke");
    frame();
    require(displayed_height > .49 && displayed_height < .51 &&
                editor.document()->source().document == original,
            "Terrain stroke did not render its isolated pressure-sensitive draft");
    io.AddMouseButtonEvent(0, false);
    frame();
    require(!editor.stroke_active() && editor.document()->revision() == before + 1,
            "Terrain release did not commit exactly one history action");
    const auto painted = editor.document()->source().document;
    require(asset_detail::read_bytes(root / locator, terrain_source_byte_limit) == disk &&
                scene.document() == expected_scene,
            "Terrain brush wrote source/scene during preview");
    click("terrain:undo");
    require(editor.document()->source().document == original, "Terrain stroke Undo failed");
    click("terrain:redo");
    require(editor.document()->source().document == painted, "Terrain stroke Redo failed");
    point("terrain:preview_image");
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    frame();
    io.AddKeyEvent(ImGuiKey_Escape, false);
    io.AddMouseButtonEvent(0, false);
    frame();
    require(!editor.stroke_active() && editor.document()->source().document == painted,
            "Escape committed an interrupted terrain stroke");
    point("terrain:preview_image");
    io.AddMouseButtonEvent(0, true);
    frame();
    locked = true;
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    require(!editor.stroke_active() && editor.document()->source().document == painted &&
                !test::ui_targets.at("terrain:save").enabled,
            "Lock did not cancel terrain stroke and protect source controls");
    locked = false;
    frame();
    click("terrain:add_layer");
    require(editor.document()->source().raster().layers.size() == 2, "Layer creation UI failed");
    click("terrain:undo");
    require(editor.document()->source().document == painted,
            "Layer Undo lost prior terrain stroke");
    number("terrain:strength", "0.25");
    require(editor.document()->source().raster().layers[0].strength == .25f,
            "Layer strength UI failed");
    click("terrain:undo");
    require(editor.document()->source().document == painted, "Strength Undo failed");
    click("terrain:save");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (editor.pending() && std::chrono::steady_clock::now() < deadline);
    require(!editor.dirty() && editor.diagnostic().empty(),
            "Terrain UI publication failed: " + editor.diagnostic());
    // Preview may inspect the Scene until the explicit authored assignment.
    point("terrain:attach");
    check_scene = false;
    io.AddMouseButtonEvent(0, true);
    frame();
    io.AddMouseButtonEvent(0, false);
    frame();
    expected_scene = scene.document();
    check_scene = true;
    require(scene.entity(actor).get<MeshRenderer>().mesh.id == editor.document()->source().asset(),
            "Published terrain assignment UI failed");
    require(scene.undo(), "Terrain assignment had no Scene Undo action");
    expected_scene = scene.document();
    require(expected_scene.at("entities") == original_scene.at("entities"),
            "Terrain assignment Scene Undo failed");
    number("terrain:strength", "0.5");
    editor.request_close();
    frame();
    click("terrain:close_cancel");
    require(editor.is_open() && editor.close_cancelled, "Terrain close Cancel lost the draft");
    editor.request_close();
    frame();
    click("terrain:close_discard");
    require(!editor.is_open() && releases && previews,
            "Terrain close Discard failed to release source preview");
    // The Mesh's rounded outer coordinate lies beyond the ideal double
    // extent here. Selection must retain the native picked tile.
    const auto float_path = std::filesystem::path("Assets/Rounded.terrain.json");
    auto rounded = TerrainDocument::create(project.writer_guard(), float_path, [](AssetId id) {
        auto source = TerrainSource::create(id, 33, 1.3);
        source.add_tile({16, 0});
        return source;
    });
    const auto rounded_tile = rounded->source().raster().tiles[1].id;
    rounded.reset();
    editor.open(project, float_path);
    click("terrain:tool");
    click("terrain:tool:0");
    picked_override = MeshBrushSample{{double(float(17 * 1.3)), 0, .5}, 1};
    click("terrain:preview_image");
    frame();
    require(displayed_tile == rounded_tile && !editor.stroke_active(),
            "Tile selection ignored the actual rounded Mesh outer boundary");
    editor.request_close();
    frame();
    click("terrain:close_discard");
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Need terrain UI scratch root");
        for (float scale : {1.f, 1.5f, 2.f})
            for (bool compact : {false, true})
                exercise(std::filesystem::absolute(argv[1]), scale, compact);
        std::cout << "Terrain mouse/key history, pressure, interruption, publication and close "
                     "passed at 100/150/200% full/compact\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
