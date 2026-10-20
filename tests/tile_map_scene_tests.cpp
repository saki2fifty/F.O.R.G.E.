#include "asset_reference_impact.hpp"
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
#include <iostream>

using namespace forge;
namespace {
void require(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void reject(F&& action) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Invalid Tile Map scene value was accepted");
}
void workflow(const std::filesystem::path& root) {
    EngineContext engine;
    Scene scene(engine.world());
    const auto actor = EntityId::generate();
    const auto map = AssetId::generate();
    Json components = Json::object();
    bool typed_map = false, optional_map = false;
    const auto schema = scene.schema();
    for (const auto& type : schema.at("components")) {
        const auto id = type.at("id").get<std::string>();
        if (id == "forge.tile_map_renderer") {
            optional_map = type.at("optional");
            for (const auto& field : type.at("fields"))
                if (field.at("id") == "map")
                    typed_map = field.at("type") == "asset_ref" &&
                                field.at("asset_type") == TileMapAsset::type;
        }
        if (id == "forge.local_translation" || id == "forge.local_rotation" ||
            id == "forge.local_scale")
            for (const auto& field : type.at("fields"))
                components[id][field.at("id").get<std::string>()] = field.at("default");
    }
    require(typed_map && optional_map,
            "Tile Map schema lost typed reference or optional component intent");
    components["forge.local_translation"]["x"] = 1000000000000.25;
    components["forge.local_scale"]["x"] = -2;
    components["forge.local_scale"]["y"] = 3;
    scene.reset({{"version", 3},
                 {"asset_id", AssetId::generate()},
                 {"entities", Json::array({{{"id", actor},
                                            {"name", "Original map actor"},
                                            {"components", components}}})}});
    const auto before = scene.document();
    authoring_command(scene, "component.add",
                      {{"entity", actor}, {"component", "forge.tile_map_renderer"}});
    const auto added = scene.document();
    require(scene.entity(actor.str()).get<TileMapRenderer>() == TileMapRenderer{} && scene.undo() &&
                scene.document() == before && scene.redo() && scene.document() == added,
            "Tile Map composition bypassed typed Scene history");
    for (const auto& [field, value] :
         std::vector<std::pair<std::string, Json>>{{"map", map},
                                                   {"layers", std::uint32_t(0x80000000)},
                                                   {"order", INT32_MIN},
                                                   {"tint_a", .5}})
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.tile_map_renderer"},
                           {"field", field},
                           {"value", value}});
    auto source = scene.document();
    source["entities"][0]["components"]["forge.tile_map_renderer"]["opaque"] = {{"keep", map}};
    scene.reset(source);
    const auto typed = scene.entity(actor.str()).get<TileMapRenderer>();
    require(typed.map.id == map && typed.layers == 0x80000000 && typed.order == INT32_MIN &&
                typed.tint_a == .5f,
            "Tile Map source and native component disagree");
    const auto authored = scene.document();
    const std::array documents{AssetReferenceDocument{"Map.scene.json", authored}};
    const auto impact = inspect_asset_references(scene.schema(), documents, {map});
    require(impact.references.size() == 1 &&
                impact.references[0].expected_type == TileMapAsset::type &&
                impact.references[0].target == map && !impact.uninspected.empty(),
            "Tile Map source reference was lost or opaque UUID text was guessed");
    const auto projected = extract_render_scene(scene.effective_document());
    if (projected.tile_maps.size() != 1 || !projected.meshes.empty() ||
        !projected.diagnostics.empty()) {
        std::cerr << "Projection counts: maps=" << projected.tile_maps.size()
                  << " meshes=" << projected.meshes.size()
                  << " sprites=" << projected.sprites.size() << '\n';
        for (const auto& diagnostic : projected.diagnostics)
            std::cerr << diagnostic.text << '\n';
    }
    require(projected.tile_maps.size() == 1 && projected.meshes.empty() &&
                projected.sprites.empty() && projected.tile_maps[0].entity == actor &&
                projected.tile_maps[0].renderer == typed &&
                projected.tile_maps[0].world.m[3] == 1000000000000.25 &&
                projected.tile_maps[0].world.m[0] == -2 && projected.tile_maps[0].world.m[5] == 3 &&
                projected.tile_maps[0].selectable && projected.diagnostics.empty(),
            "Tile Map projection lost double transforms, reflection or selection intent");
    for (const auto& [field, value] :
         std::vector<std::pair<std::string, Json>>{{"tint_a", 2},
                                                   {"tint_r", -1},
                                                   {"order", std::uint64_t(INT32_MAX) + 1},
                                                   {"layers", -1}}) {
        const auto revision = scene.revision();
        reject([&] {
            authoring_command(scene, "property.set",
                              {{"entity", actor},
                               {"component", "forge.tile_map_renderer"},
                               {"field", field},
                               {"value", value}});
        });
        require(scene.document() == authored && scene.revision() == revision,
                "Rejected map property changed source or history");
    }
    std::filesystem::create_directories(root);
    const auto path = root / "Original.scene.json";
    scene.save(path);
    Scene reopened(engine.world());
    reopened.load(path);
    require(reopened.document() == authored &&
                reopened.entity(actor.str()).get<TileMapRenderer>() == typed,
            "Tile Map save/reopen changed identity, values or unknown fields");
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.tile_map_renderer"},
                       {"field", "visible"},
                       {"value", false}});
    const auto hidden = extract_render_scene(scene.effective_document());
    require(hidden.tile_maps.size() == 1 && !hidden.tile_maps[0].renderer.visible,
            "Hidden map lost its separate draw/selection intent");
    require(scene.undo(), "Map visibility did not create an Undo step");
    authoring_command(scene, "property.set",
                      {{"entity", actor},
                       {"component", "forge.tile_map_renderer"},
                       {"field", "enabled"},
                       {"value", false}});
    require(extract_render_scene(scene.effective_document()).tile_maps.empty(),
            "Disabled map still emitted a draw projection");
    const auto member = PrefabMemberId::generate();
    const PrefabDocument prefab(
        {{"format", "forge.prefab"},
         {"version", 2},
         {"asset_id", AssetId::generate()},
         {"revision", 1u},
         {"root", member},
         {"members", Json::array({{{"id", member},
                                   {"name", "Map prefab"},
                                   {"components", authored["entities"][0]["components"]}}})}});
    Scene instances(engine.world());
    instances.set_prefab_sources({{prefab.asset(), prefab.source}});
    const auto first = instantiate_prefab(instances, prefab.asset());
    const auto second = instantiate_prefab(instances, prefab.asset());
    require(first != second && instances.entity(first).get<TileMapRenderer>() == typed &&
                instances.entity(second).get<TileMapRenderer>() == typed,
            "Map prefab instances lost shared asset intent or distinct entity identity");
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a dedicated scratch root");
        const auto root = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        workflow(root);
        std::filesystem::remove_all(root);
        std::cout << "Typed Tile Map scene/prefab/history/reference and double projection passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
