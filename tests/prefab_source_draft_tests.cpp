#include "editor/prefabs.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            return 2;
        const auto root = std::filesystem::u8path(argv[1]) / forge::AssetId::generate().str();
        std::filesystem::create_directories(root.parent_path());
        forge::SceneDocument::create_project(root, "Prefab source draft");
        forge::EngineContext engine;
        forge::Scene scene(engine.world());
        forge::SceneDocument project(scene);
        project.open_project(root);
        const auto parent =
            forge::authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Root"}})
                .at("selected")
                .get<std::string>();
        const auto child_entity =
            forge::authoring_command(scene, "entity.create", {{"kind", 0}, {"name", "Child"}})
                .at("selected")
                .get<std::string>();
        forge::authoring_command(scene, "entity.reparent",
                                 {{"entity", child_entity}, {"parent", parent}});
        const auto id = project.prefabs().create(scene, forge::create_prefab_source(scene, parent),
                                                 "Assets/Tree.prefab.json");
        forge::PrefabEditor editor;
        editor.edit_source(project, id);
        auto candidate = editor.migration_document();
        auto child = candidate.at("members").back();
        child.erase("parent");
        candidate["root"] = child.at("id");
        candidate["members"] = forge::Json::array({child});
        editor.edit_draft(candidate);
        // A subsequent property edit must reach the surviving selected member.
        editor.rename_member("Replacement root");
        if (editor.migration_document().at("members").front().at("name") != "Replacement root")
            throw std::runtime_error(
                "Removing the selected prefab member left a stale editor selection");
        const auto before = editor.migration_document();
        candidate = before;
        candidate["asset_id"] = forge::AssetId::generate();
        bool rejected = false;
        try {
            editor.edit_draft(candidate);
        } catch (const std::exception&) {
            rejected = true;
        }
        if (!rejected || editor.migration_document() != before)
            throw std::runtime_error("Invalid prefab identity changed the shared source draft");
        std::cout << "Prefab source selection and failed identity edit passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
