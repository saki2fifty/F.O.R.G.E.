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
        check(argc == 2, "module argument");
        forge::Scene scene;
        forge::Json doc = {
            {"version", 1},
            {"entities",
             forge::Json::array({{{"id", "player"},
                                  {"name", "Player"},
                                  {"components",
                                   {{"forge.position", {{"x", 0.0}, {"y", 1.0}, {"z", 2.0}}},
                                    {"missing.plugin", {{"value", 42}}}}}}})}};
        scene.replace(doc);
        check(scene.document() == doc, "round trip with unknown component");
        const auto schema = scene.schema();
        check(schema["components"][0]["fields"].size() == 3, "Flecs reflected position fields");
        check(!schema["components"][0]["fields"][0]["description"].get<std::string>().empty(),
              "property tooltips come from reflection");
        {
            forge::Scene prefabs;
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
            check(saved["entities"][0]["components"]["forge.position"]["x"] == 0,
                  "instance edit preserves prefab");
            check(saved["entities"][1]["components"]["forge.position"]["x"] == 2,
                  "instance materializes position override");
        }
        auto moved = doc;
        moved["entities"][0]["components"]["forge.position"]["x"] = 5;
        scene.edit(moved);
        check(scene.undo(), "undo exists");
        check(scene.document() == doc, "undo state");
        check(scene.redo(), "redo exists");
        check(scene.document() == moved, "redo state");
        auto invalid = moved;
        invalid["entities"].push_back(invalid["entities"][0]);
        try {
            scene.replace(invalid);
            throw std::logic_error("accepted duplicate");
        } catch (const std::runtime_error&) {
        }
        check(scene.document() == moved, "invalid edit leaves scene intact");
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
        forge::Scene restored;
        restored.load(path);
        check(restored.document() == moved, "disk round trip");
        std::filesystem::remove(path);
        forge::Module module;
        module.load(std::filesystem::absolute(argv[1]));
        ForgeHostV1 host{sizeof(ForgeHostV1), 1, &scene, [](void* p, float x, float y, float z) {
                             static_cast<forge::Scene*>(p)->translate(x, y, z);
                         }};
        module.tick(host, 0.5f);
        check(scene.document()["entities"][0]["components"]["forge.position"]["x"] == 5.5,
              "native callback changes Flecs state");
        try {
            module.load(std::filesystem::absolute("missing-library"));
            throw std::logic_error("accepted missing module");
        } catch (const std::runtime_error&) {
        }
        module.tick(host, 0.5f);
        check(scene.document()["entities"][0]["components"]["forge.position"]["x"] == 6.0,
              "failed module retains active code");
        std::cout << "core behavior passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
