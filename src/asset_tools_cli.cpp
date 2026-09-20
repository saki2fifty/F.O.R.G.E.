#include "asset_tools_cli.hpp"
#ifdef FORGE_ASSET_TOOLS
#include "bounded_json.hpp"
#include "import_authoring.hpp"
#include "self_executable.hpp"
#include "texture_authoring.hpp"
#endif
#include <forge/asset_discovery.hpp>
#include <forge/assets.hpp>
#include <iostream>

namespace forge {
int asset_tools_cli(int argc, char** argv) {
    using Json = nlohmann::json;
    try {
        if (argc < 4 || argc > 7)
            throw std::runtime_error("Usage: forge_tools --assets scan PROJECT [ROOT] | query "
                                     "PROJECT | dependents PROJECT UUID | import PROJECT SOURCE "
                                     "[OVERRIDES_JSON [IDENTITY_DECISIONS_JSON]]");
        const std::string operation = argv[2];
        const auto project = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        if (!std::filesystem::is_directory(project))
            throw std::runtime_error("Project root is not a directory");
        Json result{{"api", 1}, {"operation", operation}, {"ok", true}};
        if (operation == "scan" && argc <= 5) {
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
            for (const auto& [id, record] : catalog.records()) {
                result["assets"].push_back({{"id", id},
                                            {"type", record.type},
                                            {"source", path_utf8(record.source)},
                                            {"schema_version", record.schema_version},
                                            {"metadata", record.metadata}});
                if (record.subasset)
                    result["assets"].back()["subasset"] = {{"owner", record.subasset->owner},
                                                           {"key", record.subasset->key},
                                                           {"removed", record.subasset->removed}};
            }
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
#ifdef FORGE_ASSET_TOOLS
        } else if (operation == "import" && (argc >= 5 && argc <= 7)) {
            const auto executable = self_executable();
            auto worker = executable.parent_path() / "forge_asset_build";
#ifdef _WIN32
            worker += ".exe";
#endif
            auto registry = asset_import_registry(worker);
            auto lease = std::make_shared<ProjectLease>(project);
            AssetImportService service(lease, registry, desktop_texture_target());
            auto draft = service.prepare(std::filesystem::u8path(argv[4]));
            if (argc >= 6) {
                const std::string_view text(argv[5]);
                const auto overrides = asset_detail::parse_bounded_json(
                    std::as_bytes(std::span(text)), 65536, 4096, 16);
                if (!overrides.is_object())
                    throw std::runtime_error("Import overrides must be a JSON object");
                for (const auto& [key, value] : overrides.items())
                    draft.request.settings =
                        draft.importer->settings().edit(draft.request.settings, key, value);
            }
            std::vector<SubassetIdentityDecision> decisions;
            if (argc == 7) {
                const std::string_view text(argv[6]);
                const auto choices = asset_detail::parse_bounded_json(
                    std::as_bytes(std::span(text)), 1024 * 1024, 32768, 8);
                if (!choices.is_array() || choices.size() > 4096)
                    throw std::runtime_error("Identity decisions must be a bounded array");
                for (const auto& choice : choices) {
                    if (!choice.is_object() || choice.size() != 2 || !choice.contains("address") ||
                        !choice.contains("previous"))
                        throw std::runtime_error("Identity decision requires address and previous "
                                                 "(UUID or null for new asset)");
                    decisions.push_back(
                        {choice.at("address").get<std::string>(),
                         choice.at("previous").is_null()
                             ? std::nullopt
                             : std::optional(choice.at("previous").get<AssetId>())});
                }
            }
            const auto timeout =
                std::chrono::seconds(draft.importer->descriptor().limits.seconds + 30);
            const auto asset = draft.request.asset;
            service.submit(
                std::move(draft),
                [decisions](auto& candidate, const auto& plan, const auto& catalog) {
                    prepare_asset_publication(candidate, plan, catalog, decisions);
                },
                [](const auto&, const auto&) {});
            // CLI owns no live world/device. Importer format validation remains mandatory.
            if (!service.wait_idle(timeout))
                throw std::runtime_error("Asset import exceeded command time budget");
            auto completed = service.poll();
            if (completed.size() != 1)
                throw std::runtime_error("No import completion");
            if (!completed[0].published) {
                result["ok"] = false;
                result["error"] = {{"code", completed[0].identity_conflicts.empty()
                                                ? "asset_tool_failed"
                                                : "subasset.identity-ambiguous"},
                                   {"message", completed[0].diagnostic}};
                result["identity_conflicts"] = Json::array();
                for (const auto& conflict : completed[0].identity_conflicts)
                    result["identity_conflicts"].push_back({{"code", conflict.code},
                                                            {"type", conflict.type},
                                                            {"observations", conflict.observations},
                                                            {"previous", conflict.previous},
                                                            {"diagnostic", conflict.diagnostic}});
            }
            result["asset"] = asset;
            result["build_key"] = completed[0].job.build_key;
            result["cache_hit"] = completed[0].cache_hit;
            result["diagnostic"] = completed[0].diagnostic;
#endif
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
