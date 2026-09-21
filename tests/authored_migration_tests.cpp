#include "../src/authored_component.hpp"
#include "../src/authored_history.hpp"
#include "../src/authored_migration.hpp"
#include "../src/authored_migration_document.hpp"
#include "../src/authored_schema.hpp"
#include "../src/json_value_equal.hpp"
#include <forge/authoring.hpp>
#include <forge/prefab_authoring.hpp>
#include <forge/world.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
using namespace forge::detail;
static Json read_json(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw std::runtime_error("Cannot read migration fixture");
    return Json::parse(stream);
}
static void check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F&& action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid migration was accepted");
}
struct Counter {
    std::uint64_t count;
};
static void digest(Json& declaration) {
    declaration["digest"] = authored_structure_digest(declaration.at("structure"));
}
static Json rule(std::initializer_list<const char*> path, const char* name) {
    return {{"path", path}, {"name", name}};
}
int main() {
    try {
        EngineContext engine(WorldRole::Validation);
        auto& world = engine.world().world();
        auto native = world.component<Counter>().member<std::uint64_t>("count");
        opt_in_authoring(world, native, "project.counter", "project.game", 1,
                         {{"count", UINT64_MAX}}, "Gameplay");
        auto source = export_authored_types(world).at(0);
        auto target = source;
        target["schema_version"] = 2;
        target["structure"]["fields"][0]["id"] = "total";
        target["defaults"] = {{"total", 5u}};
        digest(target);
        Json rules = {{"aliases", Json::array({rule({"count"}, "total")})}};
        AuthoredMigration migration(world, source, target, rules);
        auto input = AuthoredCodec{0, source, {}}.defaults();
        input["opaque"] = {{"count", -1}, {"nested", Json::array({true, "keep"})}};
        const auto before = input;
        auto output = migration.migrate(input);
        check(output.at("total").get<std::uint64_t>() == UINT64_MAX && !output.contains("count") &&
                  output.at("opaque") == input.at("opaque") && input == before &&
                  output.at("$forge") == AuthoredCodec{0, target, {}}.stamp(),
              "Rename changed exact integers, opaque data, original bytes or target identity");
        input["total"] = 42;
        rejects([&] { migration.migrate(input); });
        check(input.at("total") == 42, "Failure rewrote unknown collision");
        input = before;
        input["count"] = -1;
        rejects([&] { migration.migrate(input); });
        input = before;
        input["$forge"]["schema_version"] = 3;
        rejects([&] { migration.migrate(input); });
        rejects([&] { AuthoredMigration bad(world, source, source, rules); });
        auto foreign = target;
        foreign["module"] = "another.module";
        rejects([&] { AuthoredMigration bad(world, source, foreign, rules); });
        auto corrupt = target;
        corrupt["digest"] = std::string(64, '0');
        rejects([&] { AuthoredMigration bad(world, source, corrupt, rules); });
        rejects([&] {
            AuthoredMigration bad(world, source, target,
                                  {{"aliases", Json::array({rule({"missing"}, "total")})}});
        });
        rejects([&] {
            AuthoredMigration bad(
                world, source, target,
                {{"aliases", Json::array({rule({"count"}, "total"), rule({"count"}, "total")})}});
        });
        // Removed fields survive as opaque values; newly introduced fields receive
        // only the target version's declared defaults. Partial intent gains none.
        AuthoredMigration removed(world, source, target, Json::object());
        output = removed.migrate(before);
        check(output.at("count").get<std::uint64_t>() == UINT64_MAX && output.at("total") == 5u,
              "Removed data or versioned defaults were lost");
        output = removed.migrate(before, true);
        check(output.contains("count") && !output.contains("total"),
              "Migration invented a new property override");
        auto default_change = source;
        default_change["schema_version"] = 2;
        default_change["defaults"]["count"] = 3u;
        AuthoredMigration defaults_only(world, source, default_change, Json::object());
        check(defaults_only.migrate(before).at("count").get<std::uint64_t>() == UINT64_MAX,
              "Changed defaults rewrote an existing authored value");
        auto empty_intent = Json{{"$forge", AuthoredCodec{0, source, {}}.stamp()}, {"unknown", 2}};
        check(defaults_only.migrate(empty_intent, true).size() == 2,
              "Empty known intent materialized defaults");
        // Nested collections preserve opaque members. An empty target vector's
        // default cannot supply per-element new field values implicitly.
        auto scalar = source.at("structure").at("fields").at(0);
        Json old_element{{"type", "struct"}, {"fields", Json::array({scalar})}};
        auto collection = scalar;
        collection["id"] = "items";
        collection["display_name"] = "Items";
        collection["type"] = "vector";
        collection["maximum_count"] = 4096;
        collection["element"] = old_element;
        auto nested_source = source;
        nested_source["structure"]["fields"] = Json::array({collection});
        nested_source["defaults"] = {{"items", Json::array()}};
        digest(nested_source);
        auto nested_target = nested_source;
        nested_target["schema_version"] = 2;
        nested_target["structure"]["fields"][0]["element"]["fields"][0]["id"] = "total";
        auto added = scalar;
        added["id"] = "added";
        nested_target["structure"]["fields"][0]["element"]["fields"].push_back(added);
        digest(nested_target);
        Json nested_rules{{"aliases", Json::array({rule({"items", "*", "count"}, "total")})}};
        auto nested = AuthoredCodec{0, nested_source, {}}.defaults();
        nested["items"] = Json::array(
            {{{"count", UINT64_MAX}, {"opaque", "first"}}, {{"count", 0u}, {"opaque", "second"}}});
        AuthoredMigration no_element_default(world, nested_source, nested_target, nested_rules);
        rejects([&] { no_element_default.migrate(nested); });
        nested_rules["defaults"] =
            Json::array({{{"path", {"items", "*", "added"}}, {"value", 7u}}});
        AuthoredMigration nested_migration(world, nested_source, nested_target, nested_rules);
        output = nested_migration.migrate(nested, true);
        check(output["items"][0]["total"].get<std::uint64_t>() == UINT64_MAX &&
                  output["items"][1]["total"] == 0u && output["items"][1]["added"] == 7u &&
                  output["items"][0]["opaque"] == "first" && !output["items"][0].contains("count"),
              "Nested migration lost values/extensions or failed explicit member defaults");
        // Cycles and collisions are not inferred away through field ordering.
        auto pair_source = source;
        scalar["id"] = "other";
        pair_source["structure"]["fields"].push_back(scalar);
        pair_source["defaults"]["other"] = 0u;
        digest(pair_source);
        auto pair_target = pair_source;
        pair_target["schema_version"] = 2;
        rejects([&] {
            AuthoredMigration bad(
                world, pair_source, pair_target,
                {{"aliases", Json::array({rule({"count"}, "other"), rule({"other"}, "count")})}});
        });
        AuthoredMigration collision(world, pair_source, pair_target,
                                    {{"aliases", Json::array({rule({"count"}, "other")})}});
        rejects([&] { collision.migrate(AuthoredCodec{0, pair_source, {}}.defaults()); });
        {
            EngineContext editor;
            Scene scene(editor.world());
            scene.publish_component_schemas(Json::array({source}));
            const std::string id = authoring_command(scene, "entity.create").at("selected");
            authoring_command(scene, "component.add",
                              {{"entity", id}, {"component", "project.counter"}});
            const auto prefab = create_prefab_source(scene, id);
            scene.set_prefab_sources({{prefab.asset(), prefab.source}});
            const auto instance = instantiate_prefab(scene, prefab.asset());
            // New metadata never automatically rewrites old values.
            scene.publish_component_schemas(Json::array({target}));
            const auto old_snapshot = scene.snapshot();
            AuthoredMigrationDocument document(scene.document(), source, target);
            auto converted = Json::array();
            for (const auto& v : document.values())
                converted.push_back(
                    {{"value", migration.migrate(v.at("value"), v.at("property_intent"))},
                     {"property_intent", v.at("property_intent")}});
            const auto revision = scene.revision();
            auto corrupt_output = converted;
            corrupt_output[0]["value"]["total"] = -1;
            rejects([&] { document.apply(scene, corrupt_output, revision); });
            rejects([&] { document.apply(scene, converted, revision + 1); });
            check(scene.snapshot() == old_snapshot && scene.revision() == revision,
                  "Failed/stale migration changed scene, source or revision");
            document.apply(scene, converted, revision);
            const auto changed = scene.snapshot();
            check(scene.prefab_sources().at(prefab.asset()) == prefab.source &&
                      changed != old_snapshot && scene.undo() && scene.snapshot() == old_snapshot &&
                      scene.redo() && scene.snapshot() == changed,
                  "Migration was not one scene Undo entry or changed the prefab source");
            rejects([&] { document.apply(scene, converted, revision); });
            AuthoredMigrationDocument prefab_draft(prefab.source, source, target);
            converted = Json::array();
            for (const auto& v : prefab_draft.values())
                converted.push_back(
                    {{"value", migration.migrate(v.at("value"))}, {"property_intent", false}});
            rejects([&] { prefab_draft.apply(scene, converted, scene.revision()); });
            auto revised_prefab = prefab_draft.candidate(converted);
            revised_prefab["revision"] = 2;
            bool wrote = false;
            scene.publish_prefab_sources({{prefab.asset(), revised_prefab}}, [&] { wrote = true; });
            const auto effective = scene.effective_document();
            bool inherited = false;
            for (const auto& row : effective.at("entities"))
                if (row.at("id") == instance)
                    inherited = row.at("components")
                                    .at("project.counter")
                                    .at("total")
                                    .get<std::uint64_t>() == UINT64_MAX;
            check(wrote && inherited && !scene.can_undo(),
                  "Single-prefab migration failed propagation or claimed scene Undo ownership");
        }
        {
            const auto root =
                std::filesystem::current_path() / ("schema-history-" + AssetId::generate().str());
            std::filesystem::create_directory(root);
            struct Cleanup {
                std::filesystem::path path;
                ~Cleanup() {
                    std::error_code error;
                    std::filesystem::remove_all(path, error);
                }
            } cleanup{root};
            Json manifest{{"format", "forge.authored-types"},
                          {"version", 1},
                          {"profile", "shared-native-sdk"},
                          {"fingerprint", std::string(64, 'a')},
                          {"components", Json::array({source})}};
            AuthoredHistory history(root);
            EngineContext editor;
            Scene scene(editor.world());
            auto prepared = history.prepare(manifest);
            scene.publish_component_schemas(manifest.at("components"),
                                            [&] { history.publish(prepared); });
            const auto id = authoring_command(scene, "entity.create").at("selected");
            authoring_command(scene, "component.add",
                              {{"entity", id}, {"component", "project.counter"}});
            const auto before = scene.snapshot();
            const auto revision = scene.revision();
            const auto history_before = history.document();
            const auto schema_before = scene.schema();
            manifest["components"] = Json::array({target});
            prepared = history.prepare(manifest);
            std::filesystem::create_directory(root / "forge.components.json.pending");
            rejects([&] {
                scene.publish_component_schemas(manifest.at("components"),
                                                [&] { history.publish(prepared); });
            });
            check(scene.snapshot() == before && scene.revision() == revision &&
                      scene.schema() == schema_before && history.document() == history_before &&
                      read_json(root / "forge.components.json") == history_before,
                  "Failed history write changed native schema, values, revision or prior metadata");
            std::filesystem::remove(root / "forge.components.json.pending");
            scene.publish_component_schemas(manifest.at("components"),
                                            [&] { history.publish(prepared); });
            AuthoredHistory reopened(root);
            check(reopened.declarations().size() == 2 && reopened.declarations()[0] == source,
                  "Project reopen discarded the prior migration schema");
            auto bad = manifest;
            bad["components"][0]["digest"] = std::string(64, 'b');
            rejects([&] { history.prepare(bad); });
            manifest["components"] = Json::array();
            history.publish(history.prepare(manifest));
            check(
                !history.document().at("types").at("project.counter").at("available").get<bool>() &&
                    history.declarations().size() == 2,
                "Removed schema lost its identity reservation or prior declaration");
            bad["components"][0] = target;
            bad["components"][0]["module"] = "other.owner";
            rejects([&] { history.prepare(bad); });
            auto externally_changed = history.document();
            externally_changed["external-note"] = "Keep";
            atomic_write(root / "forge.components.json", externally_changed.dump());
            rejects([&] { history.publish(history.prepare(manifest)); });
            check(read_json(root / "forge.components.json") == externally_changed,
                  "History publication overwrote an external change");
        }
        std::cout << "Explicit authored migration: identity, bounds, aliases, defaults, partial "
                     "intent, unknown retention passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
