#include "entity_recipe_tests.hpp"
#include "mesh_component_tests.hpp"
#include "node_policy_tests.hpp"
#include "ordered_hierarchy_tests.hpp"
#include "relationship_tests.hpp"
#include "render_scene_tests.hpp"
#include "render_view_tests.hpp"
#include <forge/authoring.hpp>
#include <forge/flecs_script.hpp>
#include <forge/geometry.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using forge::Json;
void check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(why);
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--script-publication") {
            std::ifstream file(argv[2]);
            const auto rejected = Json::parse(file);
            std::string source, snapshot;
            const Json good{{"ok", true}, {"entities", Json::array({{{"name", "KnownGood"}}})}};
            check(forge::publish_flecs_script_preview(good, "KnownGood {}", source, snapshot),
                  "Initial preview publication failed");
            const auto before_source = source, before_snapshot = snapshot;
            check(!forge::publish_flecs_script_preview(rejected, "Bad candidate", source, snapshot),
                  "Failed included script was published");
            check(source == before_source && snapshot == before_snapshot,
                  "Failed candidate replaced previous source/output");
            check(forge::publish_flecs_script_preview(good, "Next good {}", source, snapshot) &&
                      source == "Next good {}",
                  "Clean publication after failure did not succeed");
            std::cout << "Shared editor publication: candidate rejected, prior state retained, "
                         "recovery passed\n";
            return 0;
        }
        test_mesh_component();
        test_render_views();
        test_render_scene();
        test_node_policy();
        test_relationship_depth();
        test_entity_recipes();
        test_ordered_hierarchy();
        forge::EngineContext scene_engine;
        forge::Scene scene(scene_engine.world());
        scene.reset({{"version", 1}, {"entities", Json::array()}});
        forge::AuthoringSession api(scene);
        auto invoke = [&](const char* method, Json fields = Json::object()) {
            fields["api"] = 1;
            fields["method"] = method;
            fields["target"] = api.target();
            fields["expected_revision"] = scene.revision();
            return api.handle(fields);
        };
        const auto discovery = invoke("discover");
        check(discovery.at("ok"), "Discovery failed");
        check(discovery["result"]["commands"].size() == 27, "Command catalog incomplete");
        const auto schema = discovery["result"]["schema"];
        check(schema["components"][0]["fields"][0]["property_id"] == "forge.local_translation.x",
              "Stable property identity missing");
        check(schema["components"][2]["fields"][0]["default"] == 1, "Scale defaults missing");
        Json batch = Json::array();
        for (unsigned i = 0; i < 4; ++i)
            batch.push_back(
                {{"operation", "entity.create"}, {"arguments", {{"kind", i}, {"name", "Block"}}}});
        auto r = invoke("scene.apply", {{"commands", batch}});
        check(r.at("ok") && scene.entity_count() == 4, "Batch creation failed");
        check(scene.undo() && scene.entity_count() == 0 && !scene.undo(),
              "Batch was not one history entry");
        check(scene.redo() && scene.entity_count() == 4, "Batch redo failed");
        auto doc = scene.document();
        const auto first_id = doc["entities"][0]["id"];
        const auto second_id = doc["entities"][1]["id"];
        doc["entities"][0]["components"]["missing.extension"] = {{"payload", 42}};
        scene.reset(doc);
        const auto before = scene.document();
        const auto rev = scene.revision();
        const Json rename = {{"operation", "entity.rename"},
                             {"arguments", {{"entity", first_id}, {"name", "Changed"}}}};
        const Json bad = {{"operation", "property.set"},
                          {"arguments",
                           {{"entity", first_id},
                            {"component", "forge.scale"},
                            {"field", "x"},
                            {"value", 10001}}}};
        r = invoke("scene.apply", {{"commands", Json::array({rename, bad})}});
        check(!r.at("ok") && scene.document() == before && scene.revision() == rev && !scene.undo(),
              "Failed batch mutated scene/history");
        auto stale = Json{{"api", 1},
                          {"method", "scene.apply"},
                          {"target", api.target()},
                          {"expected_revision", rev - 1},
                          {"commands", Json::array({rename})}};
        check(api.handle(stale)["error"]["code"] == "stale_revision", "Stale write accepted");
        stale["target"]["session"] = "different";
        check(api.handle(stale)["error"]["code"] == "wrong_target", "Foreign session accepted");
        r = invoke("scene.apply", {{"commands", Json::array({rename})}});
        check(r.at("ok"), "Rename failed");
        check(scene.document()["entities"][0]["components"]["missing.extension"]["payload"] == 42,
              "Unknown data lost");
        forge::authoring_command(scene, "transform.scale",
                                 {{"entity", first_id}, {"value", {{"x", 2}, {"y", 3}, {"z", 4}}}});
        forge::authoring_command(
            scene, "transform.rotation",
            {{"entity", first_id}, {"value", {{"x", 25}, {"y", 30}, {"z", 10}}}});
        forge::authoring_command(scene, "transform.ground", {{"entity", first_id}});
        check(std::abs(forge::object_bounds(scene.effective_document()["entities"][0]).first[1]) <
                  1e-5,
              "Ground command ignored transform");
        forge::authoring_command(scene, "transform.copy_from",
                                 {{"entity", second_id}, {"source", first_id}});
        check(scene.document()["entities"][1]["components"]["forge.local_scale"]["y"] == 3,
              "Copy from failed");
        const auto query =
            invoke("entity.query", {{"component", "forge.scale"}, {"limit", 2}, {"offset", 1}});
        check(query["result"]["total"] == 4 && query["result"]["entities"].size() == 2,
              "Query paging/filter failed");
        check(!invoke("entity.query", {{"limit", 300}}).at("ok"), "Unbounded query allowed");
        auto report = forge::scene_diagnostics(scene);
        check(report["items"].size() == 4, "Diagnostics lost duplicate names/unknown data");
        Json threaded;
        std::thread worker([&] { threaded = api.handle({{"api", 1}, {"method", "discover"}}); });
        worker.join();
        check(threaded["error"]["code"] == "wrong_thread", "Wrong-thread session access accepted");
        forge::EngineContext inherited_engine;
        forge::Scene inherited(inherited_engine.world());
        auto d = scene.document();
        d["entities"][0]["prefab"] = true;
        d["entities"][1]["base"] = first_id;
        d["entities"][1]["components"].erase("forge.local_scale");
        inherited.reset(d);
        forge::authoring_command(
            inherited, "property.set",
            {{"entity", second_id}, {"component", "forge.scale"}, {"field", "x"}, {"value", 7}});
        check(inherited.document()["entities"][1]["components"]["forge.local_scale"]["y"] == 3,
              "Partial override lost inherited siblings");
        forge::authoring_command(inherited, "component.revert",
                                 {{"entity", second_id}, {"component", "forge.scale"}});
        check(!inherited.document()["entities"][1]["components"].contains("forge.local_scale"),
              "Revert failed");
        check(inherited.effective_document()["entities"][1]["components"]["forge.scale"]["x"] == 2,
              "Revert did not restore inherited value");
        auto rejected =
            invoke("scene.replace", {{"document", {{"version", 99}, {"entities", Json::array()}}}});
        check(!rejected.at("ok"), "Unsupported stored version accepted");
        std::string unicode_name;
        for (int n = 0; n < 600; ++n)
            unicode_name += "\xc3\xa9";
        check(invoke("scene.apply",
                     {{"commands",
                       Json::array(
                           {{{"operation", "entity.rename"},
                             {"arguments", {{"entity", second_id}, {"name", unicode_name}}}}})}})
                  .at("ok"),
              "UTF-8 length counted bytes instead of code points");
        // A semantic no-op does not consume a revision/history entry.
        const auto no_op = scene.revision();
        forge::authoring_command(scene, "entity.rename",
                                 {{"entity", first_id}, {"name", "Changed"}});
        check(scene.revision() == no_op, "No-op consumed revision");
        std::cout << "authoring API transactions, reflection, diagnostics, ownership and "
                     "inheritance passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
