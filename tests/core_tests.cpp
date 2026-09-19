#include "ecs_tools_tests.hpp"
#include "flecs_contract_tests.hpp"
#include "geometry_tests.hpp"
#include "reflection_tests.hpp"
#include <forge/module.hpp>
#include <forge/scene.hpp>
#include <iostream>
#include <stdexcept>
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        test_geometry();
        test_reflection();
        test_flecs_contracts();
        test_ecs_tools();
        check(argc == 2, "module argument");
        forge::EngineContext scene_engine;
        forge::Scene scene(scene_engine.world());
        forge::Json doc = {
            {"version", 1},
            {"entities",
             forge::Json::array({{{"id", "player"},
                                  {"name", "Player"},
                                  {"components",
                                   {{"forge.position", {{"x", 0.0}, {"y", 1.0}, {"z", 2.0}}},
                                    {"missing.plugin", {{"value", 42}}}}}}})}};
        scene.replace(doc);
        const auto snapshot = scene.document();
        check(scene.document() == forge::migrate_scene(doc, &snapshot),
              "round trip with unknown component");
        for (const char* axis : {"x", "y", "z"}) {
            const auto member = scene.world().component<forge::LocalTranslation>().lookup(axis);
            check(member.is_alive(), "documented reflection member must exist");
            const auto* brief = ecs_doc_get_brief(scene.world().c_ptr(), member.id());
            check(brief && std::string(brief) == std::string("Local translation along the ") +
                                                     axis + " axis, in meters.",
                  "reflection member documentation survives dependency upgrade");
        }
        const auto schema = scene.schema();
        check(schema["components"][0]["fields"].size() == 3, "Flecs reflected position fields");
        check(!schema["components"][0]["fields"][0]["description"].get<std::string>().empty(),
              "property tooltips come from reflection");
        {
            forge::EngineContext prefabs_engine;
            forge::Scene prefabs(prefabs_engine.world());
            auto prefab = doc["entities"][0];
            prefab["id"] = "base";
            prefab["prefab"] = true;
            auto instance = forge::Json{{"id", "instance"},
                                        {"name", "Instance"},
                                        {"base", "base"},
                                        {"components", forge::Json::object()}};
            prefabs.replace({{"version", 1}, {"entities", forge::Json::array({prefab, instance})}});
            prefabs.translate(2, 0, 0);
            auto saved = prefabs.document();
            check(saved["entities"][0]["components"]["forge.local_translation"]["x"] == 0,
                  "instance edit preserves prefab");
            check(saved["entities"][1]["components"]["forge.local_translation"]["x"] == 2,
                  "instance materializes position override");
        }
        {
            forge::EngineContext authored_engine;
            forge::Scene authored(authored_engine.world());
            auto hierarchy = doc;
            auto child = doc["entities"][0];
            child["id"] = "child";
            child["parent"] = "player";
            hierarchy["entities"].push_back(child);
            authored.replace(hierarchy);
            const auto expected_hierarchy = authored.document();
            authored.rename_entity("player", "Hero");
            check(authored.document()["entities"][0]["id"] == authored.canonical_id("player"),
                  "rename preserves ID");
            check(authored.undo() && authored.document() == expected_hierarchy, "rename undo");
            check(authored.redo(), "rename redo");
            const auto before = authored.document();
            for (const auto& parent : {"child", "missing", "player"}) {
                try {
                    authored.reparent_entity("player", parent);
                    throw std::logic_error("accepted invalid parent");
                } catch (const std::runtime_error&) {
                }
                check(authored.document() == before, "invalid reparent is atomic");
            }
            authored.reparent_entity("child", "");
            check(!authored.document()["entities"][1].contains("parent"), "detach to root");
            check(authored.undo() && authored.document() == before, "reparent undo");
            const auto copy = authored.duplicate_subtree("player");
            const auto duplicated = authored.document();
            check(duplicated["entities"].size() == 4, "duplicate includes descendants");
            check(duplicated["entities"][3]["parent"] == copy, "duplicate remaps parent");
            check(duplicated["entities"][2]["components"] == before["entities"][0]["components"],
                  "duplicate preserves unknown components");
            check(authored.undo() && authored.document() == before, "duplicate single undo");
            check(authored.redo() && authored.document() == duplicated,
                  "duplicate redo IDs stable");
            authored.delete_subtree(copy);
            check(authored.document() == before, "delete subtree only");
            check(authored.undo() && authored.document() == duplicated, "delete single undo");
            authored.rename_entity("player", "Hero");
            check(authored.redo(), "no-op edit preserves redo");
            check(authored.document() == before, "redo deletion after no-op");
            auto prefab_doc = hierarchy;
            prefab_doc["entities"][0]["prefab"] = true;
            prefab_doc["entities"][1].erase("parent");
            prefab_doc["entities"][1]["base"] = "player";
            authored.replace(prefab_doc);
            const auto expected_prefab = authored.document();
            try {
                authored.delete_subtree("player");
                throw std::logic_error("deleted referenced prefab");
            } catch (const std::runtime_error&) {
            }
            check(authored.document() == expected_prefab,
                  "referenced prefab deletion leaves scene intact");
            prefab_doc["entities"][1]["parent"] = "player";
            try {
                authored.replace(prefab_doc);
                throw std::logic_error("accepted recursive prefab hierarchy");
            } catch (const std::runtime_error&) {
            }
            prefab_doc["entities"][1]["parent"] = "group";
            prefab_doc["entities"][0]["parent"] = "group";
            prefab_doc["entities"].push_back(
                {{"id", "group"}, {"name", "Group"}, {"components", forge::Json::object()}});
            authored.replace(prefab_doc);
            const auto prefab_copy = authored.duplicate_subtree("group");
            check(authored.document()["entities"][4]["base"] ==
                      authored.document()["entities"][3]["id"],
                  "duplicate remaps internal prefab base");
            const auto second_copy = authored.duplicate_subtree("group");
            check(second_copy != prefab_copy, "repeated duplicates have distinct IDs");
            const auto disk = std::filesystem::current_path() / "hierarchy.scene.json";
            authored.save(disk);
            forge::EngineContext loaded_engine;
            forge::Scene loaded(loaded_engine.world());
            loaded.load(disk);
            check(loaded.document() == authored.document(), "hierarchy disk round trip");
            std::filesystem::remove(disk);
        }
        auto moved = doc;
        moved["entities"][0]["components"]["forge.position"]["x"] = 5;
        scene.edit(moved);
        const auto expected_moved = scene.document();
        check(scene.undo(), "undo exists");
        check(scene.document() == snapshot, "undo state");
        check(scene.redo(), "redo exists");
        check(scene.document() == expected_moved, "redo state");
        auto invalid = moved;
        invalid["entities"].push_back(invalid["entities"][0]);
        try {
            scene.replace(invalid);
            throw std::logic_error("accepted duplicate");
        } catch (const std::runtime_error&) {
        }
        check(scene.document() == expected_moved, "invalid edit leaves scene intact");
        invalid = moved;
        invalid["entities"][0]["parent"] = "player";
        try {
            scene.replace(invalid);
            throw std::logic_error("accepted cycle");
        } catch (const std::runtime_error&) {
        }
        const auto path = std::filesystem::current_path() / "test.scene.json";
        scene.save(path);
        scene.save(path);
        forge::EngineContext restored_engine;
        forge::Scene restored(restored_engine.world());
        restored.load(path);
        check(restored.document() == expected_moved, "disk round trip");
        std::filesystem::remove(path);
        forge::Module module;
        module.load(std::filesystem::absolute(argv[1]));
        ForgeHostV1 host{sizeof(ForgeHostV1), 1, &scene, [](void* p, float x, float y, float z) {
                             static_cast<forge::Scene*>(p)->translate(x, y, z);
                         }};
        module.tick(host, 0.5f);
        check(scene.document()["entities"][0]["components"]["forge.local_translation"]["x"] == 5.5,
              "native callback changes Flecs state");
        try {
            module.load(std::filesystem::absolute("missing-library"));
            throw std::logic_error("accepted missing module");
        } catch (const std::runtime_error&) {
        }
        module.tick(host, 0.5f);
        check(scene.document()["entities"][0]["components"]["forge.local_translation"]["x"] == 6.0,
              "failed module retains active code");
        std::cout << "core behavior passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
