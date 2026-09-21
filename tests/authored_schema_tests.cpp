#include "../src/authored_schema.hpp"
#include "../src/reconstructed_meta.hpp"
#include "../src/reflected_references.hpp"
#include <forge/world.hpp>
#include <iostream>
using namespace forge;
using namespace forge::detail;
void check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Expected authored schema rejection");
}
struct Source {
    std::uint64_t count;
    double health;
    std::string name;
    EntityRef target;
    AssetRef<MeshAsset> mesh;
};
int main() {
    try {
        EngineContext engine(WorldRole::Validation);
        auto& world = engine.world().world();
        const auto adapters = authoring_value_adapters(world);
        auto type = world.component<Source>("project.Health")
                        .member<std::uint64_t>("count")
                        .member<double>("health")
                        .member<std::string>("name")
                        .member<EntityRef>("target")
                        .member<AssetRef<MeshAsset>>("mesh");
        ecs_doc_set_name(world, type, "Friendly Health");
        ecs_doc_set_brief(world, type, "A project component, inspected outside the editor.");
        const Json defaults = {{"count", UINT64_MAX},
                               {"health", 100.0},
                               {"name", "Player"},
                               {"target", nullptr},
                               {"mesh", nullptr}};
        opt_in_authoring(world, type, "project.health", "project.game", 1, defaults, "Gameplay");
        const auto first = export_authored_types(world);
        check(first.size() == 1 && first[0].at("id") == "project.health" &&
                  first[0].at("defaults") == defaults &&
                  first[0].at("display_name") == "Friendly Health",
              "Native authoring metadata/defaults were not exported");
        validate_authored_types(world, first);
        auto corrupt = first;
        corrupt[0]["digest"] = std::string(64, '0');
        rejects([&] { validate_authored_types(world, corrupt); });
        corrupt = first;
        corrupt[0]["defaults"]["count"] = -1;
        rejects([&] { validate_authored_types(world, corrupt); });
        corrupt = first;
        corrupt.push_back(first[0]);
        rejects([&] { validate_authored_types(world, corrupt); });
        const auto structure = first[0].at("structure");
        const auto source_scene = AssetId::generate(), target_scene = AssetId::generate();
        const auto source_entity = EntityId::generate(), target_entity = EntityId::generate();
        const EntityRef ref{source_scene, source_entity};
        const AssetRef<TextureAsset> texture{AssetId::generate()};
        check(Json::parse(world.to_json(&texture).c_str()) == Json(texture.id),
              "New typed reference adapter double-encoded its UUID string");
        auto referenced = defaults;
        referenced["target"] = ref;
        referenced["mesh"] = texture.id;
        referenced["unknown"] = {{"target", ref}};
        const std::map<EntityId, EntityId> mapping{{source_entity, target_entity}};
        const auto remapped =
            remap_reflected_entity_refs(structure, referenced, source_scene, target_scene, mapping);
        check(remapped.at("target") == Json(EntityRef{target_scene, target_entity}) &&
                  remapped.at("mesh") == referenced.at("mesh") &&
                  remapped.at("unknown") == referenced.at("unknown") &&
                  referenced.at("target") == Json(ref),
              "Known reference remapping changed opaque/asset/original values");
        check(remap_reflected_entity_refs(structure, referenced, AssetId::generate(), target_scene,
                                          mapping) == referenced,
              "Foreign-scene EntityRef was rewritten");
        check(remap_reflected_entity_refs(structure, referenced, source_scene, target_scene, {}) ==
                  referenced,
              "Unmapped target was rewritten");
        const Json sequence{{"type", "vector"}, {"maximum_count", 4096}, {"element", structure}};
        check(remap_reflected_entity_refs(sequence, Json::array({referenced, defaults}),
                                          source_scene, target_scene,
                                          mapping) == Json::array({remapped, defaults}),
              "Nested collection references were not remapped or null was lost");
        auto reordered = structure;
        std::reverse(reordered["fields"].begin(), reordered["fields"].end());
        reordered["fields"][0]["display_name"] = "New friendly label";
        check(authored_structure_digest(reordered) == first[0].at("digest").get<std::string>(),
              "Physical member order/presentation label changed authored structure identity");
        reordered["fields"][0]["id"] = "different_key";
        check(authored_structure_digest(reordered) != first[0].at("digest").get<std::string>(),
              "Stable property identity change retained structure digest");
        {
            EngineContext editor(WorldRole::Authoring);
            auto& editor_world = editor.world().world();
            const auto refs = authoring_value_adapters(editor_world);
            const ReconstructedMeta copied(editor_world, structure, refs);
            const ReflectedCandidate candidate(editor_world, copied.native_type(), defaults, refs);
            check(read_reflected_native(editor_world, copied.native_type(), candidate.data(),
                                        refs) == defaults,
                  "Engine-only editor representation failed copied native defaults");
        }
        rejects([&] {
            opt_in_authoring(world, type, "project.health", "project.game", 2, defaults,
                             "Gameplay");
        });
        rejects([&] {
            opt_in_authoring(world, type, "forge.reserved", "project.game", 1, defaults,
                             "Gameplay");
        });
        rejects([&] {
            opt_in_authoring(world, type, "project.other", "project.game", 0, defaults, "Gameplay");
        });
        check(export_authored_types(world) == first, "Failed admission changed existing metadata");
        auto simple = world.component<double>();
        rejects([&] {
            opt_in_authoring(world, simple, "project.scalar", "project.game", 1, 1.0, "Gameplay");
        });
        ecs_struct_desc_t desc{};
        desc.members[0].name = "value";
        desc.members[0].type = flecs::F32;
        const auto float_type = ecs_struct_init(world, &desc);
        opt_in_authoring(world, float_type, "project.float", "project.game", 1, {{"value", .1}},
                         "Gameplay");
        const auto next = export_authored_types(world);
        check(next[0].at("defaults").at("value") == double(float(.1)),
              "f32 defaults were not normalized to actual native storage");
        desc.members[0].type = flecs::F64;
        const auto unknown = ecs_struct_init(world, &desc);
        rejects([&] {
            opt_in_authoring(world, unknown, "project.unknown", "project.game", 1,
                             {{"value", 1}, {"extra", 2}}, "Gameplay");
        });
        check(export_authored_types(world).size() == 2, "Unknown default payload was admitted");
        std::cout
            << "Native authoring opt-in, copied schema/defaults and cross-layout identity passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
