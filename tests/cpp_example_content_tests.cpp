#include "runtime_package.hpp"
#include <forge/authoring.hpp>
#include <forge/game_content.hpp>
#include <fstream>
#include <iostream>
using namespace forge;
namespace {
Json read(const std::filesystem::path& path) {
    Json value;
    std::ifstream(path) >> value;
    return value;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char** argv) {
    try {
        require(argc == 4,
                "Need prepared C++ project, matching inspected schemas and scratch root");
        const auto root = std::filesystem::absolute(argv[3]) / AssetId::generate().str();
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root, std::filesystem::copy_options::recursive);
        const auto project = read(root / "forge.project.json");
        const auto path = root / project.at("startup_scene").at("source").get<std::string>();
        const auto source = read(path);
        const std::array roots{source.at("asset_id").get<AssetId>()};
        EngineContext engine;
        Scene authored{engine.world()};
        authored.publish_component_schemas(read(argv[2]).at("components"));
        authored.restore_snapshot(source);
        const auto schema = authored.schema();
        const auto out = std::filesystem::path(root.string() + "-export");
        {
            auto lease = std::make_shared<ProjectLease>(root);
            prepare_runtime_content_catalog(*lease, roots, {}, schema);
            (void)package_runtime_content(root, out, roots, {"windows", "d3d12"}, {}, {}, schema);
            require(!std::filesystem::exists(out / "Native") &&
                        !std::filesystem::exists(out / "Scenes"),
                    "Content export shipped editable C++ or scene source directories");
            // Preserve fail-closed coverage for the unsupported sample field found
            // by Windows166. This is unknown metadata, not a new alpha feature.
            auto invalid = source;
            for (auto& row : invalid.at("entities"))
                if (row.at("components").contains("forge.tint")) {
                    row["components"]["forge.tint"]["a"] = 1.;
                    break;
                }
            std::ofstream(path) << invalid.dump(2);
            bool rejected = false;
            try {
                prepare_runtime_content_catalog(*lease, roots, {}, schema);
            } catch (const std::exception& e) {
                rejected =
                    std::string_view(e.what()).find("forge.tint.a") != std::string_view::npos;
            }
            require(rejected, "Unknown alpha no longer blocks content export");
            std::ofstream(path) << source.dump(2);
        }
        const auto moved = std::filesystem::path(out.string() + "-relocated");
        std::filesystem::rename(out, moved);
        std::filesystem::rename(root, root.string() + "-source-offline");
        const auto catalog = open_runtime_content(moved, {"windows", "d3d12"});
        require(catalog.records().size() == 1 && catalog.records().contains(roots[0]),
                "C++ example content closure contains missing/unrelated records");
        auto shipped = load_game_scene(moved, {roots[0]});
        require(shipped.at("_prefab_sources").empty(), "Unexpected C++ example prefab closure");
        shipped.erase("_prefab_sources");
        require(shipped == source, "Source-free C++ content changed values, identities or schemas");
        std::cout
            << "Compiled-schema C++ example content exports source-free; unknown alpha rejects\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
