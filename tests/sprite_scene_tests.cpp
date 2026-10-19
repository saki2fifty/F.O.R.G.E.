#include "asset_reference_impact.hpp"
#include "render_values.hpp"
#include "sprite_render_values.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/render_scene.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
void image_authoring(EngineContext& engine, Json legacy, EntityId actor,
                     const std::filesystem::path& root) {
    auto& image = legacy["entities"][0]["components"]["forge.sprite_renderer"];
    for (const auto* field : {"image_mode", "pixels_per_meter", "border_left", "border_right",
                              "border_top", "border_bottom"})
        image.erase(field);
    image["future_image_extension"] = {{"keep", Json::array({7, 9})}};
    Scene scene(engine.world());
    scene.reset(legacy);
    const auto before = scene.document();
    const auto typed = scene.entity(actor.str()).get<SpriteRenderer>();
    check(typed.image_mode == std::uint32_t(SpriteImageMode::Simple) &&
              typed.pixels_per_meter == 100 && typed.border_left == 0 && typed.border_right == 0 &&
              typed.border_top == 0 && typed.border_bottom == 0,
          "Old Sprite data did not admit additive Simple defaults");
    check(scene.document().at("asset_id") == legacy.at("asset_id") &&
              scene.document()["entities"][0]["id"].get<EntityId>() == actor &&
              scene.document()["entities"][0]["components"]["forge.sprite_renderer"]
                              ["future_image_extension"] == image["future_image_extension"],
          "Old Sprite admission changed identity or unknown data");
    for (const auto& [field, value] : std::vector<std::pair<std::string, Json>>{
             {"image_mode", 1}, {"pixels_per_meter", 32}, {"border_left", 2}, {"border_right", 1}})
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.sprite_renderer"},
                           {"field", field},
                           {"value", value}});
    const auto changed = scene.document();
    const auto mapped = scene.entity(actor.str()).get<SpriteRenderer>();
    check(mapped.image_mode == 1 && mapped.pixels_per_meter == 32 && mapped.border_left == 2 &&
              mapped.border_right == 1,
          "Image fill authoring bypassed typed component values");
    for (unsigned step = 0; step < 4; ++step)
        check(scene.undo(), "Image fill property did not create its ordinary Undo step");
    check(scene.document() == before, "Image fill Undo lost legacy/unknown intent");
    for (unsigned step = 0; step < 4; ++step)
        check(scene.redo(), "Image fill Redo failed");
    check(scene.document() == changed, "Image fill Redo changed admitted values");
    const auto path = root / "LegacyImage.scene.json";
    scene.save(path);
    Scene reopened(engine.world());
    reopened.load(path);
    check(reopened.document() == changed &&
              reopened.entity(actor.str()).get<SpriteRenderer>() == mapped,
          "Image fill values/unknown extension did not reopen exactly");
    const auto member = PrefabMemberId::generate();
    const Json prefab_source{
        {"format", "forge.prefab"},
        {"version", 2},
        {"asset_id", AssetId::generate()},
        {"revision", 1u},
        {"root", member},
        {"members", Json::array({{{"id", member},
                                  {"name", "Old image"},
                                  {"components", legacy["entities"][0]["components"]}}})}};
    const PrefabDocument prefab(prefab_source);
    check(prefab.source == prefab_source, "Legacy Sprite prefab was rewritten during admission");
    Scene instance(engine.world());
    instance.set_prefab_sources({{prefab.asset(), prefab.source}});
    const auto root_entity = instantiate_prefab(instance, prefab.asset());
    const auto inherited = instance.document();
    check(instance.entity(root_entity).get<SpriteRenderer>().image_mode == 0,
          "Legacy Sprite prefab did not realize Simple defaults");
    authoring_command(instance, "property.set",
                      {{"entity", root_entity},
                       {"component", "forge.sprite_renderer"},
                       {"field", "image_mode"},
                       {"value", 2}});
    check(instance.entity(root_entity).get<SpriteRenderer>().image_mode == 2 &&
              instance.prefab_sources().at(prefab.asset()) == prefab_source && instance.undo() &&
              instance.document() == inherited &&
              instance.entity(root_entity).get<SpriteRenderer>().image_mode == 0,
          "Sprite image override/Undo changed prefab source or inherited defaults");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected Sprite scene test directory");
        const std::filesystem::path root(argv[1]);
        std::filesystem::create_directories(root);
        EngineContext engine;
        Scene scene(engine.world());
        const auto actor = EntityId::generate();
        const auto material = AssetId::generate();
        Json components = Json::object();
        const auto schema = scene.schema();
        bool typed_material = false;
        for (const auto& component : schema.at("components")) {
            const auto id = component.at("id").get<std::string>();
            if (id != "forge.local_translation" && id != "forge.local_rotation" &&
                id != "forge.local_scale" && id != "forge.mesh_renderer")
                continue;
            for (const auto& field : component.at("fields"))
                components[id][field.at("id").get<std::string>()] = field.at("default");
        }
        components["forge.mesh_renderer"]["mesh"] = engine_primitive(8).id;
        scene.reset(
            {{"version", 3},
             {"asset_id", AssetId::generate()},
             {"entities",
              Json::array({{{"id", actor}, {"name", "Image"}, {"components", components}}})}});
        const auto before_add = scene.document();
        authoring_command(scene, "component.add",
                          {{"entity", actor}, {"component", "forge.sprite_renderer"}});
        const auto added = scene.document();
        check(scene.entity(actor.str()).has<SpriteRenderer>() &&
                  scene.entity(actor.str()).get<SpriteRenderer>() == SpriteRenderer{} &&
                  scene.undo() && scene.document() == before_add && scene.redo() &&
                  scene.document() == added,
              "Sprite component creation/Undo/Redo did not use typed Scene authority");
        image_authoring(engine, added, actor, root);
        auto source = scene.document();
        auto& sprite = source["entities"][0]["components"]["forge.sprite_renderer"];
        sprite["material"] = material;
        sprite["columns"] = sprite["rows"] = 2;
        sprite["frame"] = 3;
        sprite["width"] = 4;
        sprite["height"] = 2;
        sprite["pivot_x"] = 0;
        sprite["pivot_y"] = 0;
        sprite["tint_a"] = .5;
        scene.reset(source);
        const auto typed = scene.entity(actor.str()).get<SpriteRenderer>();
        check(typed.material.id == material && typed.frame == 3 && typed.columns == 2 &&
                  typed.rows == 2 && typed.width == 4 && typed.height == 2 && typed.tint_a == .5f,
              "Sprite codec disagrees with typed Flecs values");
        const std::array documents{AssetReferenceDocument{"Image.scene.json", scene.document()}};
        const auto references = inspect_asset_references(scene.schema(), documents, {material});
        check(references.references.size() == 1 && references.uninspected.empty() &&
                  references.references[0].expected_type == MaterialAsset::type &&
                  references.references[0].target == material,
              "Sprite material was not traversed as a typed asset dependency");
        for (const auto& component : schema.at("components"))
            if (component.at("id") == "forge.sprite_renderer")
                for (const auto& field : component.at("fields"))
                    if (field.at("id") == "material")
                        typed_material = field.at("type") == "asset_ref" &&
                                         field.at("asset_type") == MaterialAsset::type;
        check(typed_material, "Sprite schema lost native material reference metadata");
        auto extracted = extract_render_scene(scene.effective_document());
        check(extracted.sprites.size() == 1 && extracted.meshes.size() == 1 &&
                  extracted.sprites[0].entity == actor && extracted.sprites[0].renderer == typed &&
                  extracted.sprites[0].selectable && extracted.diagnostics.empty(),
              "Mesh and Sprite extraction on one entity collided or changed Sprite values");
        AffineTransform world;
        world.m = {0, -2, 0, 10, 3, 0, 0, -5, 0, 0, 1, 2};
        const auto quad = sprite_world_transform(typed, world);
        check(quad.point({-.5, .5, 0}) == Double3{10, -5, 2} &&
                  quad.point({.5, -.5, 0}) == Double3{14, 7, 2},
              "Sprite size/pivot did not compose with world rotation/scale");
        auto flipped = typed;
        flipped.flip_x = flipped.flip_y = true;
        check(sprite_world_transform(flipped, world) == quad,
              "UV flips changed Sprite geometry/pivot");
        TextureData texture;
        texture.width = 8;
        texture.height = 4;
        texture.alpha = TextureAlpha::Straight;
        const auto draw = sprite_draw_values(typed, texture);
        check(draw.region.origin == std::array<double, 2>{.5, .5} &&
                  draw.region.scale == std::array<double, 2>{.5, .5} &&
                  draw.color == std::array<float, 4>{1, 1, 1, .5f},
              "Sprite frame/tint did not resolve against source texture dimensions");
        auto extended = scene.document();
        extended["entities"][0]["components"]["forge.sprite_renderer"]["future"] = {{"keep", 7}};
        scene.reset(extended);
        authoring_command(scene, "property.set",
                          {{"entity", actor},
                           {"component", "forge.sprite_renderer"},
                           {"field", "frame"},
                           {"value", 1}});
        const auto edited = scene.document();
        check(scene.entity(actor.str()).get<SpriteRenderer>().frame == 1 && scene.undo() &&
                  scene.document() == extended && scene.redo() && scene.document() == edited,
              "Sprite property history lost extension data or frame intent");
        for (const auto& [field, value] :
             std::vector<std::pair<std::string, Json>>{{"frame", 4},
                                                       {"frame", -1},
                                                       {"frame", .5},
                                                       {"columns", 0},
                                                       {"rows", 16385},
                                                       {"width", 0},
                                                       {"height", 10001},
                                                       {"pivot_x", -1},
                                                       {"pivot_y", 1.01},
                                                       {"tint_a", 1.01},
                                                       {"tint_r", -.01},
                                                       {"image_mode", 3},
                                                       {"image_mode", .5},
                                                       {"image_mode", -1},
                                                       {"pixels_per_meter", 0},
                                                       {"pixels_per_meter", 1000001},
                                                       {"border_left", -1},
                                                       {"border_top", .25},
                                                       {"border_bottom", 16385},
                                                       {"enabled", 1},
                                                       {"material", "invalid"}}) {
            const auto revision = scene.revision();
            bool rejected = false;
            try {
                authoring_command(scene, "property.set",
                                  {{"entity", actor},
                                   {"component", "forge.sprite_renderer"},
                                   {"field", field},
                                   {"value", value}});
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && scene.revision() == revision && scene.document() == edited,
                  "Invalid Sprite property partially changed Scene/history");
        }
        const auto path = root / "Image.scene.json";
        scene.save(path);
        Scene reopened(engine.world());
        reopened.load(path);
        check(reopened.document() == edited &&
                  reopened.entity(actor.str()).get<SpriteRenderer>().frame == 1,
              "Saved Sprite scene failed exact reopen");
        const auto before_recipe = scene.document();
        const auto created =
            authoring_command(
                scene, "entity.create",
                {{"recipe", "render.sprite"},
                 {"component_values", {{"material", material}, {"columns", 2}, {"frame", 1}}}})
                .at("selected")
                .get<std::string>();
        check(scene.entity(created).has<SpriteRenderer>() &&
                  !scene.entity(created).has<MeshRenderer>() &&
                  scene.entity(created).get<SpriteRenderer>().frame == 1 && scene.undo() &&
                  scene.document() == before_recipe && scene.redo() &&
                  scene.entity(created).get<SpriteRenderer>().material.id == material,
              "Sprite recipe did not create one undoable typed composition");
        scene.undo();
        EngineContext runtime_engine(WorldRole::Runtime);
        Scene runtime(runtime_engine.world());
        runtime.restore_snapshot(edited);
        const auto live =
            runtime.tune_runtime_property(actor.str(), "forge.sprite_renderer", "frame", 2);
        runtime.tune_runtime_property(actor.str(), "forge.sprite_renderer", "flip_x", true);
        runtime.tune_runtime_property(actor.str(), "forge.sprite_renderer", "tint_a", .25);
        check(live == 2 && runtime.entity(actor.str()).get<SpriteRenderer>().frame == 2 &&
                  runtime.entity(actor.str()).get<SpriteRenderer>().flip_x &&
                  runtime.entity(actor.str()).get<SpriteRenderer>().tint_a == .25f &&
                  !runtime.undo() && !runtime.redo() && scene.document() == edited,
              "Live sprite tuning changed authoring state or failed native application");
        const auto live_native = runtime.entity(actor.str()).get<SpriteRenderer>();
        for (const auto& [field, value] :
             std::vector<std::pair<std::string, Json>>{{"frame", 4},
                                                       {"frame", 1.5},
                                                       {"frame", -1},
                                                       {"width", 0},
                                                       {"tint_a", 2},
                                                       {"material", material},
                                                       {"columns", 4},
                                                       {"enabled", false},
                                                       {"flip_x", 1}}) {
            bool rejected = false;
            try {
                runtime.tune_runtime_property(actor.str(), "forge.sprite_renderer", field, value);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && runtime.entity(actor.str()).get<SpriteRenderer>() == live_native,
                  "Rejected live Sprite edit partially changed native state");
        }
        runtime.restore_snapshot(edited);
        check(runtime.entity(actor.str()).get<SpriteRenderer>() ==
                  scene.entity(actor.str()).get<SpriteRenderer>(),
              "Fresh Play retained temporary Sprite values");
        auto corrupt = scene.effective_document();
        corrupt["entities"][0]["components"]["forge.sprite_renderer"]["frame"] = 4;
        const auto diagnosed = extract_render_scene(corrupt);
        check(diagnosed.sprites.empty() && diagnosed.meshes.size() == 1 &&
                  diagnosed.diagnostics.size() == 1 &&
                  diagnosed.diagnostics[0].context.property == "forge.sprite_renderer",
              "Malformed Sprite extraction damaged an unrelated Mesh or hid its diagnostic");
        std::cout << "Typed Sprite Scene creation/history/reopen, material references, extraction "
                     "and pivot/frame values passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
