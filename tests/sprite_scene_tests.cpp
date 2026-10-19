#include "asset_reference_impact.hpp"
#include "render_values.hpp"
#include "sprite_render_values.hpp"
#include <forge/authoring.hpp>
#include <forge/engine_assets.hpp>
#include <forge/render_scene.hpp>
#include <iostream>

using namespace forge;
namespace {
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
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
