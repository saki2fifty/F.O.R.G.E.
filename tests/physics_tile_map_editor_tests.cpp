#include "component_inspector.hpp"
#include "prefabs.hpp"
#include "tile_map_cook.hpp"
#include "tile_map_inspector.hpp"
#include <imgui_internal.h>
#include <iostream>

using namespace forge;
namespace {
void check(bool condition, const std::string& why) {
    if (!condition)
        throw std::runtime_error(why);
}
Json defaults(const char* key) {
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == key)
            return type.defaults;
    throw std::runtime_error("Missing Tile Map builtin");
}
void exercise(const std::filesystem::path& scratch, float scale) {
    constexpr std::uint64_t wide = UINT64_MAX - 1;
    auto source = TileMapValue::create(AssetId::generate(), AssetId::generate());
    auto metadata = source.metadata();
    metadata["next_id"] = UINT64_MAX;
    // Duplicate names deliberately prove that the picker selects stable keys.
    metadata["layers"][0]["name"] = "Floor";
    metadata["layers"].push_back(
        {{"id", wide}, {"name", "Floor"}, {"visible", false}, {"order", 1}});
    source = source.with_metadata(std::move(metadata));
    ui::PublishedTileMapView view{std::make_shared<const TileMapData>(decode_cooked_tile_map(
                                      cook_tile_map(source), source.asset())),
                                  "fixture-published-revision", "Published Map layers", true};
    const ui::PublishedTileMapReader read = [&](AssetRef<TileMapAsset> ref) {
        check(ref.id == source.asset(), "Picker read a different member's Map root");
        return view;
    };
    const auto root = scratch / AssetId::generate().str();
    SceneDocument::create_project(root, "Painted floor Inspector");
    EngineContext engine{WorldRole::Authoring};
    Scene scene(engine.world());
    SceneDocument project(scene);
    project.open_project(root);
    const auto actor = EntityId::generate().str();
    auto renderer = defaults("forge.tile_map_renderer");
    renderer["map"] = source.asset();
    scene.reset(
        {{"version", 5},
         {"asset_id", AssetId::generate()},
         {"entities", Json::array({{{"id", actor},
                                    {"name", "Floor"},
                                    {"components",
                                     {{"forge.tile_map_renderer", renderer},
                                      {"forge.tile_map_collider_2d", {{"layer", 1}}}}}}})}});
    const auto definition = create_prefab_source(scene, actor);
    const auto prefab_asset =
        project.prefabs().create(scene, definition, "Assets/Floor.prefab.json");
    ComponentInspector inspector;
    inspector.fixture_focus_component = "forge.tile_map_collider_2d";
    inspector.tile_map_layer = [&](const Json& components, Json& value) {
        return ui::tile_map_layer_field(read, components, value);
    };
    PrefabEditor prefab;
    prefab.tile_map_layer = inspector.tile_map_layer;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1280, 900};
    io.DeltaTime = 1.f / 60;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::style(scale);
    ui::EditorUiContext context;
    context.scene = &scene;
    ui::ContextScope scope(context);
    test::observe_ui = true;
    bool prefab_mode = false;
    const auto frame = [&] {
        test::ui_targets.clear();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({1280, 880}, ImGuiCond_Always);
        if (prefab_mode)
            prefab.draw(scene, project, false);
        else {
            ImGui::Begin("Tile collision Inspector", nullptr, ImGuiWindowFlags_NoResize);
            inspector.draw(scene, project, actor);
            ImGui::End();
        }
        ImGui::Render();
    };
    const auto point = [&](const std::string& key) {
        frame();
        check(test::ui_targets.contains(key), "Missing real layer control: " + key);
        auto box = test::ui_targets.at(key);
        const auto inside = [&] {
            const auto y = (box.minimum.y + box.maximum.y) / 2;
            return y >= box.clip_minimum.y && y <= box.clip_maximum.y;
        };
        if (!inside()) {
            auto* window = ImGui::FindWindowByName(prefab_mode ? "###Prefab source"
                                                               : "Tile collision Inspector");
            check(window, "Missing layer Inspector window");
            ImGui::SetScrollY(window, window->Scroll.y + (box.minimum.y + box.maximum.y) / 2 -
                                          (box.clip_minimum.y + box.clip_maximum.y) / 2);
            frame();
            frame();
            box = test::ui_targets.at(key);
        }
        check(inside() && box.enabled, "Layer control clipped/disabled: " + key);
        return ImVec2{(box.minimum.x + box.maximum.x) / 2, (box.minimum.y + box.maximum.y) / 2};
    };
    const auto click = [&](const std::string& key) {
        const auto at = point(key);
        io.AddMousePosEvent(at.x, at.y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
    };
    frame();
    frame();
    click("tile_map_collider:layer");
    click("tile_map_collider:layer:" + std::to_string(wide));
    check(scene.entity(actor).get<TileMapCollider2D>().layer == wide && scene.undo() &&
              scene.entity(actor).get<TileMapCollider2D>().layer == 1 && scene.redo() &&
              scene.entity(actor).get<TileMapCollider2D>().layer == wide,
          "Layer selection lost exact key/ordinary property history");
    view.current = false;
    view.status = "Preparing replacement Map";
    click("tile_map_collider:layer");
    check(test::ui_targets.contains("tile_map_collider:layer:1") &&
              !test::ui_targets.at("tile_map_collider:layer:1").enabled,
          "Stale Map offered a new collision layer");
    click("tile_map_collider:layer:none");
    check(scene.entity(actor).get<TileMapCollider2D>().layer == 0,
          "Stale resource prevented explicit unassignment");
    view.current = true;
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.tile_map_collider_2d"},
                       {"field", "layer"},
                       {"value", 42u}});
    frame();
    check(scene.entity(actor).get<TileMapCollider2D>().layer == 42,
          "Drawing a missing layer silently replaced its key");
    prefab.edit_source(project, prefab_asset);
    prefab_mode = true;
    frame();
    frame();
    click("tile_map_collider:layer");
    click("tile_map_collider:layer:" + std::to_string(wide));
    check(prefab.dirty() && prefab.migration_document()
                                    .at("members")[0]
                                    .at("components")
                                    .at("forge.tile_map_collider_2d")
                                    .at("layer") == wide,
          "Prefab source picker did not edit its exact portable layer key");
    prefab.request_save();
    frame();
    check(!prefab.dirty(), "Prefab layer key did not publish");
    check(!engine.services().available(Capability::Physics), "Authoring picker created a solver");
    test::observe_ui = false;
    ImGui::DestroyContext();
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected private Tile collision UI directory");
        std::filesystem::create_directories(argv[1]);
        for (float scale : {1.f, 1.5f, 2.f})
            exercise(argv[1], scale);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Tile collision layer selection/history/Prefab/stale/missing/wide keys at three "
                 "scales passed\n";
}
