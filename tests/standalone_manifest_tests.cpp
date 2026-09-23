#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "game_export.hpp"
#include "standalone_manifest.hpp"
#include <forge/game_settings.hpp>
#include <forge/project.hpp>
#include <forge/scene.hpp>
#include <iostream>
using namespace forge;
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Need scratch and font");
        auto check = [](bool ok, const char* why) {
            if (!ok)
                throw std::runtime_error(why);
        };
        auto reject = [&](auto f) {
            bool rejected = false;
            try {
                f();
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected, "Invalid distribution admitted");
        };
        const auto scratch = std::filesystem::absolute(argv[1]) / AssetId::generate().str();
        const auto project = scratch / "source", out = scratch / "game";
        std::filesystem::create_directories(project);
        std::filesystem::create_directories(out / "resources/ui");
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        } cleanup{scratch};
        Json scene_doc, schema;
        {
            WorldContext world;
            Scene scene(world);
            scene_doc = scene.snapshot();
            schema = scene.schema();
        }
        const auto id = scene_doc.at("asset_id").get<AssetId>();
        asset_storage::replace(project / "main.scene.json", scene_doc.dump());
        AssetCatalog source(project);
        source.add({id, "scene", "main.scene.json", scene_doc.at("version").get<unsigned>()});
        source.save(project / "forge.assets.json");
        const std::array roots{id};
        const RuntimePackageTarget target{"linux", "none"};
        package_runtime_content(project, out / "content", roots, target);
        const auto catalog = open_runtime_content(out / "content", target);
        // File-inventory fixture only, not executable/graphics acceptance.
        asset_storage::replace(out / "forge_game", "test executable bytes");
        std::filesystem::copy_file(argv[2], out / "resources/ui/LatoLatin-Regular.ttf");
        auto settings = ProjectSettings::defaults("Distribution test");
        settings["game"] = default_game_settings("forge.distribution-test", "Distribution test");
        settings["startup_scene"] = {{"asset", id},
                                     {"source", path_utf8(catalog.records().at(id).source)}};
        Json manifest{{"format", "forge.standalone"},
                      {"version", 1},
                      {"content", "content"},
                      {"target", {{"platform", "linux"}, {"backend", "none"}}},
                      {"engine",
                       {{"profile", "static-abi1"},
                        {"sdk_fingerprint", std::string(64, 'a')},
                        {"source_commit", std::string(40, 'b')},
                        {"build_id", "260923-000066"}}},
                      {"settings", settings},
                      {"assets", runtime_asset_inventory(catalog)},
                      {"files", Json::object()}};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(out))
            if (entry.is_regular_file()) {
                const auto data = asset_detail::read_bytes(entry.path(), 16 * 1024 * 1024);
                manifest["files"][path_utf8(entry.path().lexically_relative(out))] = {
                    {"bytes", data.size()}, {"sha256", asset_detail::content_digest(data)}};
            }
        auto write = [&](const Json& value) {
            asset_storage::replace(out / "forge.standalone.json", value.dump());
        };
        write(manifest);
        // Real service with a synthetic runtime kit: validates assembly/recovery,
        // not executable or graphics acceptance.
        const auto kit = scratch / "kit", exported = scratch / "exported";
        std::filesystem::create_directories(kit / "resources/ui");
        std::filesystem::copy_file(argv[2], kit / "resources/ui/LatoLatin-Regular.ttf");
        asset_storage::replace(kit / "forge_game", "test executable bytes");
        Json runtime{{"format", "forge.runtime-kit"},   {"version", 1},
                     {"executable", "forge_game"},      {"target", manifest.at("target")},
                     {"engine", manifest.at("engine")}, {"files", Json::object()}};
        for (const auto& entry : std::filesystem::recursive_directory_iterator(kit))
            if (entry.is_regular_file()) {
                const auto bytes = asset_detail::read_bytes(entry.path(), 16 * 1024 * 1024);
                runtime["files"][path_utf8(entry.path().lexically_relative(kit))] = {
                    {"bytes", bytes.size()}, {"sha256", asset_detail::content_digest(bytes)}};
            }
        asset_storage::replace(kit / "forge.runtime-kit.json", runtime.dump());
        auto project_settings = settings;
        project_settings["startup_scene"]["source"] = "main.scene.json";
        project_settings["modules"] = Json::array({"forge.physics"});
        ProjectSettings(project).save(project_settings);
        GameExportRequest request;
        request.destination = exported;
        request.runtime_kit = kit;
        request.reference_schema = schema;
        ProjectLease lease(project);
        auto result = export_standalone_game(lease, request);
        check(result.at("ok") && std::filesystem::exists(exported / "forge_game"),
              "Export assembly failed");
        const auto first = asset_storage::read(exported / "forge.standalone.json");
        std::stop_source stop;
        reject([&] {
            export_standalone_game(
                lease, request, {},
                [&](const GameExportProgress& p) {
                    if (p.completed == 4)
                        stop.request_stop();
                },
                stop.get_token());
        });
        check(asset_storage::read(exported / "forge.standalone.json") == first,
              "Cancelled export changed last good");
        asset_storage::replace(kit / "forge_game", "corrupt");
        reject([&] { export_standalone_game(lease, request); });
        check(asset_storage::read(exported / "forge.standalone.json") == first,
              "Invalid kit changed last good");
        asset_storage::replace(kit / "forge_game", "test executable bytes");
        project_settings["game"]["title"] = "Replacement";
        ProjectSettings(project).save(project_settings);
        // Use actual validated game setting field instead of accepting unknown keys.
        result = export_standalone_game(lease, request);
        (void)open_standalone_distribution(exported, target, "static-abi1", std::string(64, 'a'));
        const auto destination_name = path_utf8(exported.filename());
        const auto control = exported.parent_path() /
                             (".forge-export-" + asset_detail::content_digest(
                                                     std::as_bytes(std::span(destination_name)))
                                                     .substr(0, 24));
        const auto committed = asset_storage::read(exported / "forge.standalone.json");
        const auto hash = asset_detail::content_digest(
            asset_detail::read_bytes(exported / "forge.standalone.json", 16 * 1024 * 1024));
        std::filesystem::copy(exported, control / "candidate",
                              std::filesystem::copy_options::recursive);
        asset_storage::replace(control / "transaction.json",
                               Json{{"version", 1},
                                    {"destination", path_utf8(exported)},
                                    {"candidate", hash},
                                    {"previous", hash}}
                                   .dump());
        std::filesystem::rename(exported,
                                control / "previous"); // Process died between the two renames.
        recover_game_export(exported);
        check(asset_storage::read(exported / "forge.standalone.json") == committed &&
                  !std::filesystem::exists(control / "candidate"),
              "Interrupted promotion did not recover previous game");
        recover_game_export(exported); // Idempotent.
        std::filesystem::create_directory(scratch / "unrelated");
        asset_storage::replace(scratch / "unrelated/user.txt", "keep");
        request.destination = scratch / "unrelated";
        reject([&] { export_standalone_game(lease, request); });
        check(asset_storage::read(scratch / "unrelated/user.txt") == "keep",
              "Export overwrote unrelated directory");
        std::filesystem::rename(project, scratch / "unavailable");
        auto open = [&] {
            return open_standalone_distribution(out, target, "static-abi1", std::string(64, 'a'));
        };
        auto admitted = open();
        check(admitted.settings == settings && admitted.content == out / "content",
              "Admitted configuration changed");
        ProjectSettings runtime_settings(admitted.content, admitted.settings);
        reject([&] { runtime_settings.save(settings); });
        check(!std::filesystem::exists(out / "content/forge.project.json"),
              "Runtime created source project configuration");
        reject([&] {
            open_standalone_distribution(out, {"windows", "d3d12"}, "static-abi1",
                                         std::string(64, 'a'));
        });
        reject([&] {
            open_standalone_distribution(out, target, "shared-native-sdk", std::string(64, 'a'));
        });
        reject([&] {
            open_standalone_distribution(out, target, "static-abi1", std::string(64, 'c'));
        });
        for (unsigned test = 0; test < 4; ++test) {
            auto bad = manifest;
            if (test == 0)
                bad["settings"]["startup_scene"] = nullptr;
            if (test == 1)
                bad["assets"][0]["revision"] = std::string(64, 'f');
            if (test == 2)
                bad["content"] = "../source";
            if (test == 3)
                bad["files"]["forge_game"]["bytes"] = 0u;
            write(bad);
            reject(open);
        }
        write(manifest);
        asset_storage::replace(out / "unlisted.file", "outside declared inventory");
        reject(open);
        std::filesystem::remove(out / "unlisted.file");
        (void)open();
        std::cout
            << "Standalone manifest/configuration/profile/inventory/relocation checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
