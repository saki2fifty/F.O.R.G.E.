#include "../src/asset_reference_impact.hpp"
#include "../src/authored_component.hpp"
#include "../src/authored_schema.hpp"
#include "../src/prefab_references.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <iostream>
using namespace forge;
namespace {
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid prefab reference candidate was accepted");
}
Json& row(Json& document, const std::string& id) {
    for (auto& item : document["entities"])
        if (item.at("id") == id)
            return item;
    throw std::runtime_error("Missing reference test entity");
}
std::string create(Scene& scene, const char* name) {
    return authoring_command(scene, "entity.create", {{"name", name}}).at("selected");
}
std::string mapped(const Scene& scene, const std::string& root, const std::string& member) {
    auto document = scene.document();
    return row(document, root).at("prefab_instance").at("members").at(member);
}
Json defaults(const std::string& key) {
    for (const auto& type : detail::builtins())
        if (key == type.name)
            return type.defaults;
    throw std::runtime_error("Missing builtin test component");
}
struct NestedLink {
    EntityRef target;
};
struct ProjectLinks {
    NestedLink nested;
    EntityRef fixed[2];
};
EngineModule link_module() {
    EngineModule module;
    module.id = "project.links";
    module.dependencies = {"forge.transforms"};
    module.schema_roles = role_mask(WorldRole::Authoring) | role_mask(WorldRole::Validation) |
                          role_mask(WorldRole::Runtime);
    module.schemas = [](ModuleContext& context) {
        context.world.component<NestedLink>().member<EntityRef>("target");
        const auto type =
            context.world.component<ProjectLinks>().member<NestedLink>("nested").member<EntityRef>(
                "fixed", 2);
        detail::opt_in_authoring(
            context.world, type, "project.links", "project.links", 1,
            {{"nested", {{"target", nullptr}}}, {"fixed", Json::array({nullptr, nullptr})}},
            "Gameplay");
    };
    return module;
}
EngineModule vector_module() {
    EngineModule module;
    module.id = "project.vector_links";
    module.dependencies = {"forge.transforms"};
    module.schema_roles = role_mask(WorldRole::Authoring) | role_mask(WorldRole::Validation) |
                          role_mask(WorldRole::Runtime);
    module.schemas = [](ModuleContext& context) {
        ecs_vector_desc_t vector{};
        vector.type = context.world.id<EntityRef>();
        const auto vector_type = ecs_vector_init(context.world.c_ptr(), &vector);
        ecs_struct_desc_t structure{};
        structure.members[0].name = "targets";
        structure.members[0].type = vector_type;
        const auto type = ecs_struct_init(context.world.c_ptr(), &structure);
        detail::opt_in_authoring(context.world, type, "project.vector_links",
                                 "project.vector_links", 1, {{"targets", Json::array()}},
                                 "Gameplay");
    };
    return module;
}
void native_vector_references(const std::filesystem::path& project) {
    EngineContext engine(WorldRole::Authoring, false, {vector_module()});
    Scene scene(engine.world());
    const auto root = create(scene, "Vector root"), child = create(scene, "Vector child");
    scene.reparent_entity(child, root, ReparentMode::KeepLocal);
    const auto& codec = engine.world().authored_codecs().front();
    auto value = codec.defaults();
    const EntityRef root_ref{scene.asset_id(), EntityId::parse(root)};
    value["targets"] =
        Json::array({root_ref, EntityRef{scene.asset_id(), EntityId::parse(child)}, nullptr});
    value["opaque"] = root_ref;
    auto document = scene.document();
    row(document, child)["components"][codec.key()] = value;
    scene.edit(document);
    const auto source = create_prefab_source(scene, root);
    PrefabLibrary library(project);
    const auto asset = library.create(scene, source, "NativeVector.prefab.json");
    std::string child_member;
    for (const auto& member : source.source.at("members"))
        if (member.at("name") == "Vector child")
            child_member = member.at("id");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
    for (const auto& instance : {first, second}) {
        const auto actor = mapped(scene, instance, child_member);
        const auto native = codec.read(scene.entity(actor), true);
        check(native.at("targets") ==
                  Json::array({EntityRef{scene.asset_id(), EntityId::parse(instance)},
                               EntityRef{scene.asset_id(), EntityId::parse(actor)}, nullptr}),
              "Native reflected vector references crossed instance boundaries");
        auto saved = scene.document();
        check(!row(saved, actor).at("components").contains(codec.key()),
              "Native vector reference materialization became authored ownership");
    }
    EngineContext runtime(WorldRole::Runtime, false, {vector_module()});
    Scene loaded(runtime.world());
    loaded.restore_snapshot(scene.snapshot());
    const auto& runtime_codec = runtime.world().authored_codecs().front();
    const auto actor = mapped(scene, second, child_member);
    check(runtime_codec.read(loaded.entity(actor), true).at("targets") ==
              codec.read(scene.entity(actor), true).at("targets"),
          "Native runtime vector storage lost normalized references on recovery");
}
void custom_references(const std::filesystem::path& project) {
    EngineContext engine(WorldRole::Authoring, false, {link_module()});
    Scene scene(engine.world());
    const auto root = create(scene, "Custom root"), child = create(scene, "Custom child");
    scene.reparent_entity(child, root, ReparentMode::KeepLocal);
    const auto& codec = engine.world().authored_codecs().front();
    auto value = codec.defaults();
    const EntityRef root_ref{scene.asset_id(), EntityId::parse(root)};
    const EntityRef child_ref{scene.asset_id(), EntityId::parse(child)};
    value["nested"]["target"] = root_ref;
    value["fixed"] = Json::array({root_ref, child_ref});
    value["opaque"] = root_ref;
    auto document = scene.document();
    row(document, child)["components"][codec.key()] = value;
    scene.edit(document);
    const auto source = create_prefab_source(scene, root);
    PrefabLibrary library(project);
    const auto asset = library.create(scene, source, "Custom.prefab.json");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
    std::string member;
    for (const auto& item : source.source.at("members"))
        if (item.at("name") == "Custom child")
            member = item.at("id");
    const auto a = mapped(scene, first, member), b = mapped(scene, second, member);
    check(scene.entity(a).get<ProjectLinks>().nested.target ==
                  EntityRef{scene.asset_id(), EntityId::parse(first)} &&
              scene.entity(b).get<ProjectLinks>().fixed[1] ==
                  EntityRef{scene.asset_id(), EntityId::parse(b)},
          "Nested/array custom member references crossed instance boundaries");
    auto saved = scene.document();
    check(!row(saved, a).at("components").contains(codec.key()),
          "Custom reference materialization became authored ownership");
    auto effective = scene.effective_document();
    check(row(effective, a).at("components").at(codec.key()).at("opaque") == Json(root_ref),
          "Custom reference traversal rewrote an opaque extension");
    authoring_command(scene, "component.override", {{"entity", a}, {"component", codec.key()}});
    auto owned = scene.document();
    check(row(owned, a).at("components").contains(codec.key()),
          "Explicit custom reference component intent was elided");
    effective = scene.effective_document();
    check(row(effective, a).at("components").at(codec.key()).at("nested").at("target") ==
              Json(EntityRef{scene.asset_id(), EntityId::parse(first)}),
          "Effective projection lost explicit custom reference component values");
    check(scene.undo() && scene.document() == saved,
          "Custom whole-component Undo did not restore reference inheritance");
    auto stale_document = saved;
    Json stale = {{"$forge", codec.stamp()}, {"nested", {{"unavailable", Json(root_ref)}}}};
    stale["$forge"]["digest"] = "unavailable-schema";
    row(stale_document, a)["property_overrides"][codec.key()] = stale;
    scene.edit(stale_document);
    auto stale_saved = scene.document();
    check(row(stale_saved, a).at("property_overrides").at(codec.key()) == stale &&
              !row(stale_saved, a).at("components").contains(codec.key()),
          "Reference materialization interpreted an unadmitted custom property mask");
    check(scene.undo() && scene.document() == saved,
          "Opaque mask edit Undo lost custom reference inheritance");
    EngineContext runtime(WorldRole::Runtime, false, {link_module()});
    Scene loaded(runtime.world());
    loaded.restore_snapshot(scene.snapshot());
    check(loaded.entity(a).get<ProjectLinks>().fixed[0] ==
              EntityRef{scene.asset_id(), EntityId::parse(first)},
          "Runtime custom codec did not receive normalized member references");
    const auto independent = source.duplicate(scene.schema());
    for (const auto& item : independent.source.at("members"))
        if (item.at("name") == "Custom child") {
            const auto ref = item.at("components")
                                 .at(codec.key())
                                 .at("nested")
                                 .at("target")
                                 .get<PrefabMemberRef>();
            check(ref.prefab == independent.asset() && ref.member == independent.root(),
                  "Admitted custom source duplication lost its member remap");
        }
    authoring_command(
        scene, "property.set",
        {{"entity", a},
         {"component", codec.key()},
         {"field", "nested"},
         {"value", {{"target", EntityRef{scene.asset_id(), EntityId::parse(first)}}}}});
    auto partial = scene.document();
    row(partial, a)["property_overrides"][codec.key()]["future.intent"] = Json(root_ref);
    scene.edit(partial);
    auto native = scene.entity(a).get<ProjectLinks>();
    native.fixed[1] = {scene.asset_id(), EntityId::parse(b)};
    scene.entity(a).set(native);
    const auto mutation = scene.snapshot();
    auto mutation_copy = mutation;
    check(row(mutation_copy, a).at("property_overrides").at(codec.key()).at("future.intent") ==
              Json(root_ref),
          "Custom native mutation discarded opaque partial intent");
    loaded.restore_snapshot(mutation);
    check(loaded.entity(a).get<ProjectLinks>().fixed[1] == native.fixed[1],
          "Custom native mutation outside an existing field mask was lost on recovery");
    // A typed vector follows the same walker; unknown vector-shaped data is untouched.
    auto schema = scene.schema();
    for (auto& component : schema["components"])
        if (component.at("id") == codec.key())
            component["fields"].push_back(
                {{"id", "list"},
                 {"type", "vector"},
                 {"maximum_count", 4096},
                 {"element", {{"type", "entity_ref"}, {"nullable", true}}}});
    Json components = {{codec.key(), value}};
    components[codec.key()]["list"] = Json::array({root_ref, child_ref, nullptr});
    unsigned visited = 0;
    detail::transform_prefab_component_references(components, schema, [&](const Json& ref) {
        ++visited;
        return ref;
    });
    check(visited == 5 && components.at(codec.key()).at("opaque") == Json(root_ref),
          "Declared collection traversal missed references or interpreted opaque values");
}
void test(const std::filesystem::path& project) {
    std::filesystem::remove_all(project);
    std::filesystem::create_directories(project);
    EngineContext engine;
    Scene scene(engine.world());
    const auto root = create(scene, "Sequence rig"), door = create(scene, "Door"),
               body = create(scene, "Body");
    scene.reparent_entity(door, root, ReparentMode::KeepLocal);
    scene.reparent_entity(body, root, ReparentMode::KeepLocal);
    auto document = scene.document();
    const EntityRef controller{scene.asset_id(), EntityId::parse(root)};
    auto sequence = defaults("forge.animation_sequence");
    sequence["duration"] = 5.0;
    row(document, root)["components"]["forge.animation_sequence"] = sequence;
    auto animator = defaults("forge.transform_animator");
    animator.update({{"sequence", controller}, {"sequence_start_seconds", .5}});
    row(document, door)["components"]["forge.transform_animator"] = animator;
    auto joint = defaults("forge.physics_joint");
    joint.update(
        {{"body_a", EntityRef{scene.asset_id(), EntityId::parse(body)}}, {"body_b", controller}});
    row(document, door)["components"]["forge.physics_joint"] = joint;
    const Json opaque = {{"sequence", controller}, {"future", "unchanged"}};
    row(document, door)["components"]["unavailable.links"] = opaque;
    scene.edit(document);
    const auto definition = create_prefab_source(scene, root);
    check(definition.source.at("version") == 3, "Portable capture lacked source version gate");
    const std::array reference_inputs{
        AssetReferenceDocument{"Rig.prefab.json", definition.source},
        AssetReferenceDocument{"Source.scene.json", scene.document()}};
    const auto impact =
        inspect_asset_references(scene.schema(), reference_inputs, {scene.asset_id()});
    check(impact.references.size() == 3 && !impact.uninspected.empty(),
          "Typed reference impact confused prefab members with Scene asset dependencies");
    for (const auto& hit : impact.references)
        check(hit.source == "Source.scene.json" && hit.expected_type == SceneAsset::type,
              "Prefab member link escaped into external asset reference impact");
    std::string door_member, body_member;
    for (const auto& member : definition.source.at("members")) {
        if (member.at("name") == "Door")
            door_member = member.at("id");
        if (member.at("name") == "Body")
            body_member = member.at("id");
    }
    PrefabLibrary library(project);
    const auto asset = library.create(scene, definition, "SequenceRig.prefab.json");
    const auto first = instantiate_prefab(scene, asset), second = instantiate_prefab(scene, asset);
    const auto a = mapped(scene, first, door_member), b = mapped(scene, second, door_member);
    auto flagged = scene.document();
    row(flagged, first)["name_override"] = true;
    row(flagged, a)["missing_member"] = false;
    const std::array flagged_input{AssetReferenceDocument{"Flags.scene.json", flagged}};
    const auto flag_impact = collect_asset_references(scene.schema(), flagged_input);
    for (const auto& field : flag_impact.uninspected)
        check(field.find("name_override") == std::string::npos &&
                  field.find("missing_member") == std::string::npos,
              "Declared prefab state flags were classified as opaque dependency payloads");
    row(flagged, first)["name_override"] = {{"future", asset}};
    const std::array malformed_flag_input{AssetReferenceDocument{"Future.scene.json", flagged}};
    const auto malformed_flags = collect_asset_references(scene.schema(), malformed_flag_input);
    check(std::any_of(
              malformed_flags.uninspected.begin(), malformed_flags.uninspected.end(),
              [](const auto& field) { return field.find("name_override") != std::string::npos; }),
          "Unknown flag-shaped payload was silently admitted as a scalar flag");
    const auto expected_a = EntityRef{scene.asset_id(), EntityId::parse(first)};
    const auto expected_b = EntityRef{scene.asset_id(), EntityId::parse(second)};
    check(scene.entity(a).get<TransformAnimator>().sequence == expected_a &&
              scene.entity(b).get<TransformAnimator>().sequence == expected_b,
          "Instances did not resolve their own sequence controllers");
    check(scene.entity(a).get<PhysicsJoint>().body_a ==
                  EntityRef{scene.asset_id(), EntityId::parse(mapped(scene, first, body_member))} &&
              scene.entity(b).get<PhysicsJoint>().body_b == expected_b,
          "Joint links did not resolve through their own instance maps");
    const auto saved = scene.document();
    check(!row(document = saved, a).at("components").contains("forge.transform_animator") &&
              !row(document, a).at("components").contains("forge.physics_joint"),
          "Native reference materialization manufactured authored component ownership");
    auto effective = scene.effective_document();
    check(row(effective, a).at("components").at("unavailable.links") == opaque,
          "Opaque references were interpreted during capture");
    const auto snapshot = scene.snapshot();
    EngineContext recovered_engine;
    Scene recovered(recovered_engine.world());
    recovered.restore_snapshot(snapshot);
    check(recovered.document() == saved &&
              recovered.entity(a).get<TransformAnimator>().sequence == expected_a,
          "Save/recovery changed inherited reference intent");

    authoring_command(scene, "property.set",
                      {{"entity", a},
                       {"component", "forge.transform_animator"},
                       {"field", "sequence"},
                       {"value", expected_a}});
    auto explicit_intent = scene.document();
    check(row(explicit_intent, a)
              .at("property_overrides")
              .at("forge.transform_animator")
              .contains("sequence"),
          "Equal reference override lost explicit intent");
    check(scene.undo() && scene.document() == saved, "Undo lost reference inheritance");
    check(scene.redo() && scene.document() == explicit_intent, "Redo lost reference override");
    authoring_command(
        scene, "property.revert",
        {{"entity", a}, {"component", "forge.transform_animator"}, {"field", "sequence"}});
    auto reverted = scene.document();
    // Existing Revert keeps an empty mask container; it carries no override intent.
    for (auto& item : reverted["entities"])
        if (item.value("property_overrides", Json::object()).empty())
            item.erase("property_overrides");
    check(reverted == saved && scene.entity(a).get<TransformAnimator>().sequence == expected_a,
          "Revert did not restore inherited member link");

    auto changed = library.source(asset);
    for (auto& member : changed["members"])
        if (member.at("id") == door_member) {
            member["name"] = "Renamed door";
            member["components"]["forge.transform_animator"]["sequence_start_seconds"] = 1.25;
        }
    library.publish(scene, library.source(asset), changed);
    check(scene.entity(a).get<TransformAnimator>().sequence_start_seconds == 1.25 &&
              scene.entity(b).get<TransformAnimator>().sequence == expected_b,
          "Source publication did not update inherited materialized components");
    const auto copy = scene.duplicate_subtree(first);
    check(scene.entity(mapped(scene, copy, door_member)).get<TransformAnimator>().sequence ==
              EntityRef{scene.asset_id(), EntityId::parse(copy)},
          "Instance duplicate borrowed the original controller");
    const auto duplicated = library.duplicate(scene, asset, "Independent.prefab.json");
    const auto duplicate_source = library.source(duplicated);
    for (const auto& member : duplicate_source.at("members"))
        if (member.at("name") == "Renamed door") {
            const auto ref = member.at("components")
                                 .at("forge.transform_animator")
                                 .at("sequence")
                                 .get<PrefabMemberRef>();
            check(ref.prefab == duplicated &&
                      ref.member == duplicate_source.at("root").get<PrefabMemberId>(),
                  "Source duplicate failed to remap its admitted member link");
            check(member.at("components").at("unavailable.links") == opaque,
                  "Source duplicate rewrote opaque references");
        }

    // A removed referenced body retains its mapping in old and freshly placed instances.
    auto missing = library.source(asset);
    auto& members = missing["members"];
    members.erase(
        std::remove_if(members.begin(), members.end(),
                       [&](const Json& member) { return member.at("id") == body_member; }),
        members.end());
    const auto old_body = mapped(scene, first, body_member);
    library.publish(scene, library.source(asset), missing);
    check(!scene.entity(old_body) &&
              scene.entity(a).get<PhysicsJoint>().body_a.entity == EntityId::parse(old_body),
          "Removed member reference did not retain an unresolved identity");
    const auto fresh = instantiate_prefab(scene, asset);
    const auto fresh_body = mapped(scene, fresh, body_member);
    check(!scene.entity(fresh_body) &&
              scene.entity(mapped(scene, fresh, door_member)).get<PhysicsJoint>().body_a.entity ==
                  EntityId::parse(fresh_body),
          "New instance did not persist a missing target mapping");
    const auto missing_saved = scene.document();
    scene.reset(missing_saved);
    check(mapped(scene, fresh, body_member) == fresh_body,
          "Missing member target was regenerated during reload");
    const auto missing_copy = scene.duplicate_subtree(fresh);
    const auto missing_copy_body = mapped(scene, missing_copy, body_member);
    check(missing_copy_body != fresh_body && !scene.entity(missing_copy_body) &&
              scene.entity(mapped(scene, missing_copy, door_member))
                      .get<PhysicsJoint>()
                      .body_a.entity == EntityId::parse(missing_copy_body),
          "Missing member duplication borrowed another instance's tombstone identity");

    auto foreign = definition.source;
    for (auto& member : foreign["members"])
        if (member.at("id") == door_member)
            member["components"]["forge.transform_animator"]["sequence"] = expected_a;
    rejects([&] { (void)PrefabDocument(foreign); });
    foreign = definition.source;
    foreign["version"] = 2;
    rejects([&] { (void)PrefabDocument(foreign); });
    auto collision = Json(controller);
    collision["member"] = "future payload";
    rejects([&] {
        (void)detail::capture_prefab_reference(collision, scene.asset_id(), asset,
                                               {{root, definition.root().str()}});
    });
    check(collision.at("member") == "future payload", "Failed conversion overwrote opaque data");
    auto mixed = definition.source;
    for (auto& member : mixed["members"])
        if (member.at("id") == door_member)
            member["components"]["forge.transform_animator"]["sequence"]["scene"] =
                scene.asset_id();
    const std::array mixed_input{AssetReferenceDocument{"Mixed.prefab.json", mixed}};
    rejects([&] { (void)collect_asset_references(scene.schema(), mixed_input); });
    // Capturing only the door cannot silently retain its scene controller/body links.
    rejects([&] { (void)create_prefab_source(scene, door); });

    authoring_command(scene, "property.set",
                      {{"entity", a},
                       {"component", "forge.transform_animator"},
                       {"field", "sequence_start_seconds"},
                       {"value", 1.25}});
    auto masked = scene.document();
    row(masked, a)["property_overrides"]["forge.transform_animator"]["future.intent"] = opaque;
    scene.edit(masked);
    auto live = scene.entity(a).get<TransformAnimator>();
    live.sequence = expected_b;
    scene.entity(a).set(live);
    const auto mutated = scene.snapshot();
    auto mutation_intent = mutated;
    check(row(mutation_intent, a)
                  .at("property_overrides")
                  .at("forge.transform_animator")
                  .at("future.intent") == opaque,
          "Native reference mutation discarded opaque property intent");
    recovered.restore_snapshot(mutated);
    check(recovered.entity(a).get<TransformAnimator>().sequence == expected_b,
          "Recovery lost an actual native reference mutation");
    auto cleared = library.source(asset);
    for (auto& member : cleared["members"])
        if (member.at("id") == door_member)
            member["components"]["forge.transform_animator"]["sequence"] = nullptr;
    library.publish(scene, library.source(asset), cleared);
    const auto fresh_actor = mapped(scene, fresh, door_member);
    authoring_command(scene, "component.override",
                      {{"entity", fresh_actor}, {"component", "forge.transform_animator"}});
    auto null_whole = scene.document();
    row(null_whole, fresh_actor)["components"]["forge.transform_animator"]["future.component"] =
        opaque;
    scene.edit(null_whole);
    auto null_effective = scene.effective_document();
    check(row(null_effective, fresh_actor)
                  .at("components")
                  .at("forge.transform_animator")
                  .at("future.component") == opaque,
          "Cleared reference whole-component projection discarded opaque extensions");
    check(scene.undo() && scene.undo() && !scene.entity(fresh_actor).owns<TransformAnimator>(),
          "Cleared reference whole-override Undo did not restore native inheritance");
    authoring_command(scene, "property.set",
                      {{"entity", fresh_actor},
                       {"component", "forge.transform_animator"},
                       {"field", "sequence_start_seconds"},
                       {"value", .25}});
    auto from_null = scene.entity(fresh_actor).get<TransformAnimator>();
    check(!from_null.sequence.entity, "Source clear did not remove inherited controller");
    from_null.sequence = expected_b;
    scene.entity(fresh_actor).set(from_null);
    const auto null_snapshot = scene.snapshot();
    recovered.restore_snapshot(null_snapshot);
    check(recovered.entity(fresh_actor).get<TransformAnimator>().sequence == expected_b,
          "A native reference assigned after source clear was lost behind a partial mask");
    auto returned = library.source(asset);
    for (const auto& member : definition.source.at("members"))
        if (member.at("id") == body_member)
            returned["members"].push_back(member);
    library.publish(scene, library.source(asset), returned);
    check(mapped(scene, fresh, body_member) == fresh_body && scene.entity(fresh_body).is_alive() &&
              scene.entity(missing_copy_body).is_alive(),
          "Returning member did not restore each instance's retained target identity");
    const auto whole = duplicate_scene_asset(scene.document());
    EngineContext whole_engine;
    Scene whole_copy(whole_engine.world());
    auto whole_snapshot = scene.snapshot();
    whole_snapshot.update(whole);
    whole_copy.restore_snapshot(whole_snapshot);
    check(whole_copy.asset_id() != scene.asset_id(), "Whole scene copy reused its identity");
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Provide isolated reference fixture directory");
        test(argv[1]);
        custom_references(std::filesystem::path(argv[1]) / "custom");
        native_vector_references(std::filesystem::path(argv[1]) / "vector");
        std::cout << "Portable Prefab reference lifecycle passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
