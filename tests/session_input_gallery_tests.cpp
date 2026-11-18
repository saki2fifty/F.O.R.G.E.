#include "runtime_dependencies.hpp"
#include <array>
#include <forge/scene.hpp>
#include <fstream>
#include <iostream>

using namespace forge;
namespace {
constexpr std::array paths{"Scenes/235-session-input-a.scene.json",
                           "Scenes/236-session-input-b.scene.json"};
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void prepare(const std::filesystem::path& root) {
    ProjectLease lease(root);
    auto catalog = AssetCatalog::open_project(root);
    std::array<AssetId, 2> scenes;
    for (unsigned i = 0; i < paths.size(); ++i) {
        WorldContext world;
        Scene scene(world);
        scene.load(root / paths[i]);
        scenes[i] = scene.asset_id();
        scene.save(root / paths[i]);
        auto record = catalog.records().at(scenes[i]);
        record.schema_version = scene.snapshot().at("version").get<unsigned>();
        catalog.replace(std::move(record));
    }
    catalog.save(AssetCatalog::project_index(root));
    for (unsigned i = 0; i < scenes.size(); ++i)
        catalog = declare_runtime_dependencies(lease, scenes[i],
                                               {{scenes[1 - i],
                                                 "scene",
                                                 AssetDependencyKind::Runtime,
                                                 "declared:Session input next Scene",
                                                 {}}},
                                               catalog.document());
    for (const auto scene : scenes)
        validate_runtime_declarations(root, catalog.records().at(scene));
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--prepare") {
            prepare(std::filesystem::absolute(argv[2]));
            std::cout << "Session input Gallery sources and declarations prepared\n";
            return 0;
        }
        check(argc == 3, "Expected Gallery and scratch directory");
        const auto gallery = std::filesystem::absolute(argv[1]);
        const auto root = std::filesystem::absolute(argv[2]) / AssetId::generate().str();
        std::filesystem::create_directories(root / "Scenes");
        for (const auto* name : {"forge.assets.json", "forge.project.json", paths[0], paths[1]})
            std::filesystem::copy_file(gallery / name, root / name);
        const auto before = AssetCatalog::open_project(root).document();
        prepare(root);
        const auto catalog = AssetCatalog::open_project(root);
        check(catalog.records().size() == before.at("assets").size(),
              "Gallery declaration preparation changed identity count");
        WorldContext world;
        Scene scene(world);
        scene.load(root / paths[0]);
        const auto record = catalog.records().at(scene.asset_id());
        std::ofstream(root / paths[0], std::ios::app) << '\n';
        bool stale = false;
        try {
            validate_runtime_declarations(root, record);
        } catch (const std::exception& error) {
            stale = std::string_view(error.what()).find("export.declarations.stale") !=
                    std::string_view::npos;
        }
        check(stale, "Edited Gallery source silently retained an old dependency review");
        std::filesystem::remove_all(root);
        std::cout << "Session input Scene identities and cyclic runtime declarations passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
