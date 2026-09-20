#include "asset_tools_cli.hpp"
#include <forge/asset_discovery.hpp>
#include <forge/assets.hpp>
#include <iostream>

namespace forge {
int asset_tools_cli(int argc, char** argv) {
    using Json = nlohmann::json;
    try {
        if (argc < 4 || argc > 5)
            throw std::runtime_error("Usage: forge_tools --assets scan PROJECT [ROOT] | query "
                                     "PROJECT | dependents PROJECT UUID");
        const std::string operation = argv[2];
        const auto project = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        if (!std::filesystem::is_directory(project))
            throw std::runtime_error("Project root is not a directory");
        Json result{{"api", 1}, {"operation", operation}, {"ok", true}};
        if (operation == "scan") {
            SourceScanOptions options;
            if (argc == 5)
                options.roots = {std::filesystem::u8path(argv[4])};
            const auto scan = scan_asset_sources(project, options);
            result["ok"] = scan.complete;
            result["complete"] = scan.complete;
            result["bytes_read"] = scan.bytes_read;
            result["filtered"] = scan.filtered;
            result["files"] = Json::array();
            result["diagnostics"] = Json::array();
            for (const auto& [path, file] : scan.files) {
                result["files"].push_back({{"source", path_utf8(path)},
                                           {"source_kind", file.source_kind},
                                           {"bytes", file.bytes},
                                           {"digest", file.digest},
                                           {"alias_of", path_utf8(file.alias_of)}});
            }
            for (const auto& diagnostic : scan.diagnostics)
                result["diagnostics"].push_back(
                    {{"source", path_utf8(diagnostic.source)},
                     {"code", diagnostic.code},
                     {"message", diagnostic.message},
                     {"severity", diagnostic.error ? "error" : "warning"}});
        } else if (operation == "query" && argc == 4) {
            const auto catalog = AssetCatalog::open_project(project);
            result["assets"] = Json::array();
            for (const auto& [id, record] : catalog.records())
                result["assets"].push_back({{"id", id},
                                            {"type", record.type},
                                            {"source", path_utf8(record.source)},
                                            {"schema_version", record.schema_version},
                                            {"metadata", record.metadata}});
            result["dependencies"] = catalog.dependency_graph().document();
        } else if (operation == "dependents" && argc == 5) {
            const auto catalog = AssetCatalog::open_project(project);
            const auto id = AssetId::parse(argv[4]);
            result["asset"] = id;
            result["registered"] = catalog.records().contains(id);
            result["direct"] = catalog.dependency_graph().referrers(id);
            result["transitive"] = catalog.dependency_graph().invalidated_by(id);
        } else if (operation == "source-dependents" && argc == 5) {
            const auto catalog = AssetCatalog::open_project(project);
            const auto source = ProjectPaths::normalize(std::filesystem::u8path(argv[4]));
            (void)ProjectPaths(project).resolve(source);
            result["source"] = path_utf8(source);
            result["direct"] = catalog.dependency_graph().source_referrers(source);
            result["affected"] = catalog.dependency_graph().invalidated_by_source(source);
        } else {
            throw std::runtime_error("Unknown asset operation or incorrect argument count");
        }
        std::cout << result.dump(2) << '\n';
        return result.at("ok").get<bool>() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cout << Json{{"api", 1},
                          {"ok", false},
                          {"error", {{"code", "asset_tool_failed"}, {"message", error.what()}}}}
                         .dump(2)
                  << '\n';
        return 1;
    }
}
} // namespace forge
