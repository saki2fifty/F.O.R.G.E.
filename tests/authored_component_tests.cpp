#include "../src/authored_component.hpp"
#include "../src/authored_schema.hpp"
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/world.hpp>
#include <iostream>
using namespace forge;
using namespace forge::detail;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct ProjectValue {
    std::string title;
    std::uint64_t count = 0;
    EntityRef target;
};
int main() {
    const char* stage = "source registration";
    try {
        Json copied;
        {
            EngineContext source(WorldRole::Validation);
            auto& world = source.world().world();
            const auto type = world.component<ProjectValue>()
                                  .member<std::string>("title")
                                  .member<std::uint64_t>("count")
                                  .member<EntityRef>("target");
            opt_in_authoring(world, type, "project.value", "project.game", 1,
                             {{"title", "Ready"}, {"count", UINT64_MAX}, {"target", nullptr}},
                             "Gameplay");
            copied = export_authored_types(world);
        }
        // Source world and its project type are gone. Only copied metadata enters
        // the authoring composition; native values use engine-owned lifecycle hooks.
        stage = "copied registration";
        EngineContext editor(WorldRole::Authoring, false, {copied_authoring_module(copied)});
        auto& world = editor.world().world();
        stage = "codec discovery";
        const auto codecs = authored_codecs(world);
        check(codecs.size() == 1, "Copied type was not admitted");
        const auto& codec = codecs.front();
        auto value = codec.defaults();
        value["future"] = {{"unknown", 7}};
        stage = "candidate preparation";
        auto candidate = codec.prepare(world, value);
        check(candidate.has_value(), "Matching value was not prepared");
        auto base = world.prefab();
        codec.apply(base, candidate);
        candidate.reset();
        auto instance = world.entity().is_a(base);
        check(instance.has(codec.native_type) && !instance.owns(codec.native_type) &&
                  codec.read(instance, true).at("count") == UINT64_MAX,
              "Native component inheritance failed");
        auto equal = codec.prepare(world, value);
        codec.apply(instance, equal);
        check(instance.owns(codec.native_type), "Equal-value override lost explicit ownership");
        check(codec.merge(codec.read(instance, false), codec.extensions(value)) == value,
              "Unknown extension did not survive native storage projection");
        codec.apply(instance, std::nullopt);
        check(!instance.owns(codec.native_type) && instance.has(codec.native_type),
              "Revert failed to return to native inheritance");
        auto invalid = value;
        invalid["count"] = -1;
        bool rejected = false;
        try {
            (void)codec.prepare(world, invalid);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && codec.read(instance, true).at("count") == UINT64_MAX,
              "Invalid candidate changed a live native value");
        auto changed = value;
        changed["$forge"]["schema_version"] = 2;
        check(!codec.prepare(world, changed), "Mismatched schema became live native state");
        changed = value;
        changed.erase("$forge");
        check(!codec.prepare(world, changed), "Unversioned opaque value was silently admitted");
        stage = "scene authoring";
        Scene scene(editor.world());
        const std::string selected = authoring_command(scene, "entity.create").at("selected");
        authoring_command(scene, "component.add",
                          {{"entity", selected}, {"component", codec.key()}});
        check(scene.entity(selected).owns(codec.native_type),
              "Add Component did not create native state");
        auto saved = scene.document();
        saved["entities"][0]["components"][codec.key()]["future"] = {{"unknown", 7}};
        scene.edit(saved);
        check(scene.document() == saved,
              "Known native values/unknown fields failed scene round trip");
        const auto before_bad = scene.document();
        const auto revision = scene.revision();
        rejected = false;
        try {
            authoring_command(scene, "property.set",
                              {{"entity", selected},
                               {"component", codec.key()},
                               {"field", "count"},
                               {"value", -1}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == before_bad && scene.revision() == revision,
              "Invalid scene edit changed native values or history revision");
        authoring_command(scene, "property.set",
                          {{"entity", selected},
                           {"component", codec.key()},
                           {"field", "title"},
                           {"value", "Edited"}});
        check(scene.undo() && scene.document() == saved && scene.redo(),
              "Custom property Undo/Redo failed");
        saved = scene.document();
        const auto ref = scene.reference(selected);
        authoring_command(scene, "property.set",
                          {{"entity", selected},
                           {"component", codec.key()},
                           {"field", "target"},
                           {"value", ref}});
        saved = scene.document();
        saved["entities"][0]["components"][codec.key()]["future"]["target"] = ref;
        scene.reset(saved);
        const auto duplicate_id = scene.duplicate_subtree(selected);
        check(codec.read(scene.entity(duplicate_id), true).at("target") ==
                  Json(scene.reference(duplicate_id)),
              "Subtree duplication did not remap declared custom EntityRef");
        const auto duplicate_asset = AssetId::generate();
        const auto duplicate_doc = duplicate_scene_asset(saved, duplicate_asset, scene.schema());
        const auto duplicate_ref =
            EntityRef{duplicate_asset, duplicate_doc.at("entities")[0].at("id").get<EntityId>()};
        check(duplicate_doc.at("entities")[0].at("components").at(codec.key()).at("target") ==
                      Json(duplicate_ref) &&
                  duplicate_doc.at("entities")[0]
                          .at("components")
                          .at(codec.key())
                          .at("future")
                          .at("target") == Json(ref),
              "Scene asset duplication lost known reference remapping or rewrote opaque payload");
        scene.reset(saved);
        {
            EngineContext absent;
            Scene unknown(absent.world());
            unknown.reset(saved);
            check(unknown.document() == saved, "Missing module discarded authored component data");
        }
        auto incompatible = saved;
        incompatible["entities"][0]["components"][codec.key()]["$forge"]["schema_version"] = 2;
        scene.reset(incompatible);
        check(scene.document() == incompatible && !scene.entity(selected).has(codec.native_type),
              "Changed schema was silently interpreted or discarded");
        rejected = false;
        try {
            authoring_command(scene, "property.set",
                              {{"entity", selected},
                               {"component", codec.key()},
                               {"field", "title"},
                               {"value", "Lost"}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == incompatible,
              "Incompatible component remained editable");
        rejected = false;
        try {
            authoring_command(scene, "component.revert",
                              {{"entity", selected}, {"component", codec.key()}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == incompatible,
              "Revert discarded a read-only incompatible component");
        scene.reset(saved);
        stage = "native runtime realization";
        EngineModule native;
        native.id = "project.game";
        native.dependencies = {"forge.transforms"};
        native.schemas = [](ModuleContext& context) {
            const auto type = context.world.component<ProjectValue>()
                                  .member<std::string>("title")
                                  .member<std::uint64_t>("count")
                                  .member<EntityRef>("target");
            opt_in_authoring(context.world, type, "project.value", "project.game", 1,
                             {{"title", "Ready"}, {"count", UINT64_MAX}, {"target", nullptr}},
                             "Gameplay");
        };
        {
            EngineContext runtime(WorldRole::Runtime, false, {native});
            Scene loaded(runtime.world());
            loaded.reset(saved);
            unsigned matches = 0;
            runtime.world().world().each([&](const ProjectValue& data) {
                check(data.title == "Edited" && data.count == UINT64_MAX && data.target == ref,
                      "Native gameplay query did not receive authored named values");
                ++matches;
            });
            check(matches == 1, "Native gameplay query did not find the authored component");
            rejected = false;
            try {
                loaded.reset(incompatible);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && loaded.document() == saved,
                  "Runtime accepted a changed authored schema");
        }
        {
            EngineContext runtime(WorldRole::Runtime);
            Scene loaded(runtime.world());
            rejected = false;
            try {
                loaded.reset(saved);
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected && loaded.entity_count() == 0,
                  "Runtime silently ignored a missing authored module");
        }
        stage = "structured prefab authoring";
        const auto prefab = create_prefab_source(scene, selected);
        scene.set_prefab_sources({{prefab.asset(), prefab.source}});
        const auto instance_id = instantiate_prefab(scene, prefab.asset());
        check(scene.entity(instance_id).has(codec.native_type) &&
                  !scene.entity(instance_id).owns(codec.native_type),
              "Structured prefab did not inherit native custom values");
        const auto authored_title = saved["entities"][0]["components"][codec.key()]["title"];
        authoring_command(scene, "property.set",
                          {{"entity", instance_id},
                           {"component", codec.key()},
                           {"field", "title"},
                           {"value", authored_title}});
        check(scene.entity(instance_id).owns(codec.native_type),
              "Equal property intent did not materialize native override");
        auto revised = prefab.source;
        revised["revision"] = 2;
        revised["members"][0]["components"][codec.key()]["title"] = "New source";
        revised["members"][0]["components"][codec.key()]["count"] = 7;
        scene.set_prefab_sources({{prefab.asset(), revised}});
        const auto effective = codec.read(scene.entity(instance_id), true);
        check(effective.at("title") == authored_title && effective.at("count") == 7,
              "Prefab publication lost explicit property intent or failed to update inherited "
              "fields");
        const auto before_failed_source = scene.snapshot();
        auto bad_source = revised;
        bad_source["revision"] = 3;
        bad_source["members"][0]["components"][codec.key()]["count"] = -1;
        rejected = false;
        try {
            scene.set_prefab_sources({{prefab.asset(), bad_source}});
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.snapshot() == before_failed_source,
              "Failed prefab candidate changed the live scene or source revision");
        authoring_command(
            scene, "property.revert",
            {{"entity", instance_id}, {"component", codec.key()}, {"field", "title"}});
        check(!scene.entity(instance_id).owns(codec.native_type) &&
                  codec.read(scene.entity(instance_id), true).at("title") == "New source",
              "Property Revert failed to resume native prefab inheritance");
        check(scene.undo() &&
                  codec.read(scene.entity(instance_id), true).at("title") == authored_title,
              "Revert Undo did not restore explicit equal-value intent");
        // Leave entities alive: copied native type scopes must survive until their
        // world retires all owned strings, including inherited/overridden values.
        std::cout << "Copied component native storage, inheritance, intent and unknown "
                     "preservation passed\n";
    } catch (const std::exception& error) {
        std::cerr << stage << ": " << error.what() << '\n';
        return 1;
    }
}
