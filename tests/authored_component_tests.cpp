#include "../src/authored_component.hpp"
#include "../src/authored_schema.hpp"
#include "../src/json_value_equal.hpp"
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
        check(!json_value_equal(Json(UINT64_MAX), Json(-1)) &&
                  !json_value_equal(Json(UINT64_C(9007199254740993)), Json(9007199254740992.0)) &&
                  json_value_equal(Json(7u), Json(7)),
              "Authored equality narrowed or rounded an integer");
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
        auto invalid_direct = before_bad;
        invalid_direct["entities"][0]["components"][codec.key()]["count"] = -1;
        rejected = false;
        try {
            scene.edit(invalid_direct);
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && scene.document() == before_bad && scene.revision() == revision,
              "Direct scene replacement confused an invalid integer with unchanged data");
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
        auto invalid_unchanged_revision = prefab.source;
        invalid_unchanged_revision["members"][0]["components"][codec.key()]["count"] = -1;
        bool wrote_invalid = false;
        rejected = false;
        try {
            scene.publish_prefab_sources({{prefab.asset(), invalid_unchanged_revision}},
                                         [&] { wrote_invalid = true; });
        } catch (const std::exception&) {
            rejected = true;
        }
        check(rejected && !wrote_invalid,
              "Prefab no-op comparison permitted an invalid candidate's durable write");
        const auto instance_id = instantiate_prefab(scene, prefab.asset());
        const auto inherited_view = scene.effective_document();
        for (const auto& row : inherited_view.at("entities"))
            if (row.at("id") == instance_id)
                check(row.at("components").at(codec.key()).at("future") ==
                          saved.at("entities")[0].at("components").at(codec.key()).at("future"),
                      "Inherited prefab opaque extensions disappeared from the copied view");
        {
            EngineContext absent;
            Scene opaque_prefab(absent.world());
            opaque_prefab.restore_snapshot(scene.snapshot());
            const auto missing_view = opaque_prefab.effective_document();
            for (const auto& row : missing_view.at("entities"))
                if (row.at("id") == instance_id)
                    check(row.at("components").at(codec.key()) ==
                              saved.at("entities")[0].at("components").at(codec.key()),
                          "Missing schema hid an inherited prefab payload");
            check(opaque_prefab.snapshot() == scene.snapshot(),
                  "Missing-schema effective view created an authored override");
        }
        check(scene.entity(instance_id).has(codec.native_type) &&
                  !scene.entity(instance_id).owns(codec.native_type),
              "Structured prefab did not inherit native custom values");
        const auto authored_title = saved["entities"][0]["components"][codec.key()]["title"];
        const auto inherited_snapshot = scene.snapshot();
        authoring_command(scene, "component.override",
                          {{"entity", instance_id}, {"component", codec.key()}});
        check(scene.entity(instance_id).owns(codec.native_type) &&
                  codec.read(scene.entity(instance_id), true).at("title") == authored_title,
              "Explicit equal-value component override did not own native values");
        check(scene.undo() && scene.snapshot() == inherited_snapshot && scene.redo() &&
                  scene.entity(instance_id).owns(codec.native_type),
              "Whole-component override Undo/Redo failed");
        authoring_command(scene, "component.revert",
                          {{"entity", instance_id}, {"component", codec.key()}});
        check(!scene.entity(instance_id).owns(codec.native_type),
              "Whole-component Revert did not resume inheritance");
        authoring_command(scene, "property.set",
                          {{"entity", instance_id},
                           {"component", codec.key()},
                           {"field", "title"},
                           {"value", authored_title}});
        check(scene.entity(instance_id).owns(codec.native_type),
              "Equal property intent did not materialize native override");
        const auto partial_snapshot = scene.snapshot();
        authoring_command(scene, "component.override",
                          {{"entity", instance_id}, {"component", codec.key()}});
        const auto whole_snapshot = scene.document();
        for (const auto& row : whole_snapshot.at("entities"))
            if (row.at("id") == instance_id)
                check(row.at("components").contains(codec.key()) &&
                          !row.value("property_overrides", Json::object()).contains(codec.key()),
                      "Explicit whole override retained competing partial intent");
        check(scene.undo() && scene.snapshot() == partial_snapshot,
              "Undo whole override did not restore partial intent");
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
        stage = "owner-thread schema publication";
        {
            EngineContext managed;
            Scene authored(managed.world());
            authored.publish_component_schemas(copied);
            const auto id =
                authoring_command(authored, "entity.create").at("selected").get<std::string>();
            authoring_command(authored, "component.add",
                              {{"entity", id}, {"component", "project.value"}});
            const auto definition = create_prefab_source(authored, id);
            authored.set_prefab_sources({{definition.asset(), definition.source}});
            const auto instance = instantiate_prefab(authored, definition.asset());
            authoring_command(authored, "property.set",
                              {{"entity", instance},
                               {"component", "project.value"},
                               {"field", "title"},
                               {"value", "Ready"}});
            const auto before = authored.snapshot();
            const auto before_revision = authored.revision();
            const auto old_type = managed.world().authored_codecs().front().native_type;
            authored.publish_component_schemas(copied);
            check(authored.revision() == before_revision && authored.snapshot() == before,
                  "Unchanged schema publication changed scene revision or contents");
            auto invalid = copied;
            invalid[0]["defaults"]["count"] = -1;
            rejected = false;
            try {
                authored.publish_component_schemas(invalid);
            } catch (const std::exception&) {
                rejected = true;
            }
            if (!(rejected && authored.snapshot() == before &&
                  authored.revision() == before_revision && authored.can_undo() &&
                  authored.entity(instance).owns(old_type)))
                std::cerr << "Rejected=" << rejected
                          << " snapshot_equal=" << (authored.snapshot() == before)
                          << " revision=" << authored.revision() << "/" << before_revision
                          << " undo=" << authored.can_undo()
                          << " owns=" << authored.entity(instance).owns(old_type) << '\n';
            check(rejected && authored.snapshot() == before &&
                      authored.revision() == before_revision && authored.can_undo() &&
                      authored.entity(instance).owns(old_type),
                  "Failed schema candidate changed scene, native values or history");
            auto changed_defaults = copied;
            changed_defaults[0]["defaults"]["title"] = "New default";
            authored.publish_component_schemas(changed_defaults);
            const auto replacement = managed.world().authored_codecs().front().native_type;
            check(
                replacement != old_type && !authored.world().is_alive(old_type) &&
                    authored.snapshot() == before && authored.entity(instance).owns(replacement),
                "Schema replacement leaked old types or changed existing authored/default intent");
            check(authored.undo() && !authored.entity(instance).owns(replacement) &&
                      authored.redo(),
                  "Schema-only publication lost scene Undo/Redo");
            authored.publish_component_schemas(Json::array());
            check(!authored.world().is_alive(replacement) && authored.snapshot() == before,
                  "Removing schema availability discarded preserved native authored values");
            authored.publish_component_schemas(copied);
            check(authored.snapshot() == before &&
                      authored.entity(instance).has(
                          managed.world().authored_codecs().front().native_type),
                  "Restoring schema availability failed to realize preserved values/prefabs");
            const auto stable = authored.revision();
            {
                Scene another(managed.world());
                rejected = false;
                try {
                    authored.publish_component_schemas(changed_defaults);
                } catch (const std::exception&) {
                    rejected = true;
                }
                check(rejected && authored.revision() == stable,
                      "Schema publication silently invalidated another active scene");
            }
            rejected = false;
            std::thread foreign([&] {
                try {
                    authored.publish_component_schemas(changed_defaults);
                } catch (const std::exception&) {
                    rejected = true;
                }
            });
            foreign.join();
            check(rejected, "Schema publication admitted a foreign owner thread");
        }
        // Leave entities alive: copied native type scopes must survive until their
        // world retires all owned strings, including inherited/overridden values.
        std::cout << "Copied component native storage, inheritance, intent and unknown "
                     "preservation passed\n";
    } catch (const std::exception& error) {
        std::cerr << stage << ": " << error.what() << '\n';
        return 1;
    }
}
