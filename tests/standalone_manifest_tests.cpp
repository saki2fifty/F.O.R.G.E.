#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "fog_volume_package_tests.hpp"
#include "game_export.hpp"
#include "render_values.hpp"
#include "runtime_document_package.hpp"
#include "runtime_workers_package_tests.hpp"
#include "standalone_manifest.hpp"
#include <forge/game_settings.hpp>
#include <forge/input_message.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <forge/scene.hpp>
#include <forge/shader_asset.hpp>
#include <iostream>
using namespace forge;
int main(int argc, char** argv) {
    try {
        if (argc != 3 && argc != 5)
            throw std::runtime_error("Need scratch and font");
        auto check = [](bool ok, const char* why) {
            if (!ok)
                throw std::runtime_error(why);
        };
        auto reject = [&](auto f, const char* reason = "Invalid distribution admitted") {
            bool rejected = false;
            try {
                f();
            } catch (const std::exception&) {
                rejected = true;
            }
            check(rejected, reason);
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
        test_fog_package::exercise(scratch, check, reject);
        test_worker_package::exercise(scratch, check, reject);
        {
            using package_detail::document_runtime_features;
            MeshWind wind;
            const auto renderer = Json{{"wind", detail::render_value(wind)}};
            auto row = Json{{"id", EntityId::generate()},
                            {"components", {{"forge.mesh_renderer", renderer}}}};
            const auto none = Json::object();
            const Json required{{"mesh_wind", mesh_wind_profile_version}};
            check(document_runtime_features(Json{{"entities", Json::array({row})}}) == none,
                  "Default wind requires a newer runtime kit");
            row["components"]["forge.mesh_renderer"]["wind"]["enabled"] = true;
            for (const char* channel : {"components", "property_overrides"}) {
                auto entry = row;
                if (std::string_view(channel) != "components") {
                    entry[channel] = entry["components"];
                    entry["components"] = Json::object();
                }
                for (const char* rows : {"entities", "members", "overrides"}) {
                    Json document{{rows, Json::array({entry})}};
                    if (std::string_view(rows) == "overrides")
                        document["base"] = AssetId::generate();
                    check(document_runtime_features(document) == required,
                          "Scene/Prefab/variant wind intent omitted runtime requirements");
                }
            }
            const Json unrelated{
                {"entities", Json::array({{{"components", {{"custom.wind", row}}}}})}};
            check(document_runtime_features(unrelated) == none,
                  "Custom project data was interpreted as renderer wind");
            AssetCatalog features(scratch / "wind-compatibility");
            AssetRecord document{AssetId::generate(), "scene", "wind.scene.json", 5};
            document.metadata["forge.runtime_document"] = {{"runtime_features", required}};
            features.add(document);
            validate_runtime_compatibility(features,
                                           {{"runtime_mesh_wind", mesh_wind_profile_version}});
            reject([&] { validate_runtime_compatibility(features, Json::object()); },
                   "Wind project accepted a kit without geometry support");
            for (const Json& profile : {Json(), Json(false), Json(0), Json(1.), Json("1"), Json(2)})
                reject([&] {
                    validate_runtime_compatibility(features, {{"runtime_mesh_wind", profile}});
                });
            for (const Json& invalid : {Json(), Json::array(), Json{{"mesh_wind", 2}},
                                        Json{{"mesh_wind", 1.}}, Json{{"future_geometry", 1}}}) {
                document.metadata["forge.runtime_document"]["runtime_features"] = invalid;
                features.replace(document);
                reject(
                    [&] { validate_runtime_compatibility(features, {{"runtime_mesh_wind", 1}}); });
            }
        }
        {
            using package_detail::document_runtime_features;
            const auto none = Json::object();
            const Json required{{"input_message_triggers", input_message_trigger_profile_version}};
            for (const char* rows : {"entities", "members", "overrides"})
                for (const char* channel : {"components", "property_overrides"}) {
                    Json document{
                        {rows,
                         Json::array({{{channel, {{"forge.input_message", Json::object()}}}}})}};
                    if (std::string_view(rows) == "overrides")
                        document["base"] = AssetId::generate();
                    auto& message = document[rows][0][channel]["forge.input_message"];
                    check(document_runtime_features(document) == none,
                          "Legacy Input Message requires a newer kit");
                    message["trigger"] = 0;
                    check(document_runtime_features(document) == none,
                          "Default Pressed trigger requires a newer kit");
                    for (unsigned trigger : {1u, 2u}) {
                        message["trigger"] = trigger;
                        check(document_runtime_features(document) == required,
                              "Scene/Prefab/variant omitted trigger runtime requirements");
                    }
                    for (const Json& invalid :
                         {Json(), Json(false), Json(-1), Json(3), Json(0.), Json(1.),
                          Json("Changed"), Json(std::uint64_t{1} << 32)}) {
                        message["trigger"] = invalid;
                        reject([&] { (void)document_runtime_features(document); },
                               "Invalid trigger produced runtime compatibility metadata");
                    }
                }
            const Json combined{{"mesh_wind", mesh_wind_profile_version},
                                {"input_message_triggers", input_message_trigger_profile_version}};
            const Json document{
                {"entities",
                 Json::array({{{"components",
                                {{"forge.mesh_renderer", {{"wind", {{"enabled", true}}}}},
                                 {"forge.input_message", {{"trigger", 2}}}}}}})}};
            check(document_runtime_features(document) == combined,
                  "Input Message requirements hid renderer requirements");
            AssetCatalog features(scratch / "trigger-compatibility");
            AssetRecord record{AssetId::generate(), "scene", "trigger.scene.json", 5};
            features.add(record);
            validate_runtime_compatibility(features, none);
            record.metadata["forge.runtime_document"] = {{"runtime_features", required}};
            features.replace(record);
            validate_runtime_compatibility(features, {{"runtime_input_message_triggers",
                                                       input_message_trigger_profile_version}});
            reject([&] { validate_runtime_compatibility(features, none); },
                   "Trigger project accepted a kit without trigger support");
            for (const Json& invalid : {Json(), Json(false), Json(-1), Json(0), Json(1.), Json("1"),
                                        Json(2), Json(std::uint64_t{1} << 32)}) {
                reject([&] {
                    validate_runtime_compatibility(features,
                                                   {{"runtime_input_message_triggers", invalid}});
                });
                record.metadata["forge.runtime_document"]["runtime_features"] = {
                    {"input_message_triggers", invalid}};
                features.replace(record);
                reject([&] {
                    validate_runtime_compatibility(features,
                                                   {{"runtime_input_message_triggers", 1}});
                });
                record.metadata["forge.runtime_document"]["runtime_features"] = required;
                features.replace(record);
            }
            record.metadata["forge.runtime_document"]["runtime_features"] = combined;
            features.replace(record);
            validate_runtime_compatibility(
                features, {{"runtime_mesh_wind", 1}, {"runtime_input_message_triggers", 1}});
        }
        {
            // Metadata admission uses the existing typed graph, without realizing
            // a graphics device or opening source files for a surface requirement.
            AssetCatalog features(scratch / "surface-compatibility");
            AssetRecord shader{AssetId::generate(), "shader", "Surface.shader.json", 1};
            shader.metadata["forge.shader"] = {
                {"surface_wrapper_version", current_surface_wrapper_version}};
            features.add(shader);
            validate_runtime_compatibility(
                features, {{"runtime_surface_wrapper", current_surface_wrapper_version}});
            validate_runtime_compatibility(
                features, {{"runtime_surface_wrapper", current_surface_wrapper_version + 1}});
            reject([&] { validate_runtime_compatibility(features, Json::object()); },
                   "New surface accepted a kit without an advertised renderer profile");
            for (const Json& profile :
                 {Json(), Json(false), Json(0), Json(current_surface_wrapper_version - 1),
                  Json(15.), Json("15")})
                reject([&] {
                    validate_runtime_compatibility(features,
                                                   {{"runtime_surface_wrapper", profile}});
                });
            for (const Json& version : {Json(), Json(false), Json(-1), Json(0), Json(15.),
                                        Json(current_surface_wrapper_version + 1), Json("15")}) {
                shader.metadata["forge.shader"]["surface_wrapper_version"] = version;
                features.replace(shader);
                reject([&] {
                    validate_runtime_compatibility(
                        features, {{"runtime_surface_wrapper", current_surface_wrapper_version}});
                });
            }
            shader.metadata["forge.shader"]["surface_wrapper_version"] =
                first_explicit_surface_runtime_profile - 1;
            features.replace(shader);
            validate_runtime_compatibility(features, Json::object());
            shader.metadata["forge.shader"].erase("surface_wrapper_version");
            features.replace(shader);
            validate_runtime_compatibility(features, Json::object());
        }
        {
            // Metadata admission uses the existing typed graph, without realizing
            // ECS or interpreting an unrelated project file as a UI document.
            AssetCatalog routed(scratch / "ui-compatibility");
            const AssetRecord output{AssetId::generate(), "render_texture",
                                     "Image.rendertexture.json", 1};
            routed.add(output);
            AssetRecord document{AssetId::generate(), "prefab", "Panel.prefab.json", 1};
            document.dependencies = {output.id};
            document.dependency_edges = {{output.id,
                                          "render_texture",
                                          AssetDependencyKind::Runtime,
                                          "document:forge.ui_document.render_texture",
                                          {}}};
            routed.add(document);
            validate_runtime_compatibility(routed, {{"runtime_ui_protocol", 2}});
            reject([&] { validate_runtime_compatibility(routed, Json::object()); });
            for (const Json& protocol :
                 {Json(), Json(1), Json(2.0), Json("2"), Json(true), Json(3)})
                reject([&] {
                    validate_runtime_compatibility(routed, {{"runtime_ui_protocol", protocol}});
                });
            validate_runtime_compatibility(catalog, Json::object());
            AssetRecord stylesheet{AssetId::generate(), "ui_stylesheet", "filters.rcss", 1};
            stylesheet.metadata["forge.ui_source"] = {{"runtime_features", {{"filters", 1}}}};
            routed.add(stylesheet);
            validate_runtime_compatibility(routed,
                                           {{"runtime_ui_protocol", 2}, {"runtime_ui_filters", 1}});
            reject([&] { validate_runtime_compatibility(routed, {{"runtime_ui_protocol", 2}}); });
            for (const Json& profile : {Json(), Json(0), Json(1.0), Json("1"), Json(true), Json(3)})
                reject([&] {
                    validate_runtime_compatibility(
                        routed, {{"runtime_ui_protocol", 2}, {"runtime_ui_filters", profile}});
                });
            for (const Json& features : {Json(), Json::array(), Json{{"filters", 1.0}},
                                         Json{{"filters", 3}}, Json{{"future_effect", 1}}}) {
                stylesheet.metadata["forge.ui_source"]["runtime_features"] = features;
                routed.replace(stylesheet);
                reject([&] {
                    validate_runtime_compatibility(
                        routed, {{"runtime_ui_protocol", 2}, {"runtime_ui_filters", 1}});
                });
            }
            stylesheet.metadata["forge.ui_source"]["runtime_features"] = {{"filters", 1}};
            routed.replace(stylesheet);
            validate_runtime_compatibility(routed,
                                           {{"runtime_ui_protocol", 2}, {"runtime_ui_filters", 2}});
            stylesheet.metadata["forge.ui_source"]["runtime_features"] = {{"filters", 2}};
            routed.replace(stylesheet);
            validate_runtime_compatibility(routed,
                                           {{"runtime_ui_protocol", 2}, {"runtime_ui_filters", 2}});
            reject([&] {
                validate_runtime_compatibility(
                    routed, {{"runtime_ui_protocol", 2}, {"runtime_ui_filters", 1}});
            });
        }
        // File-inventory fixture only, not executable/graphics acceptance.
        asset_storage::replace(out / "forge_game", "test executable bytes");
        std::filesystem::copy_file(argv[2], out / "resources/ui/LatoLatin-Regular.ttf");
        auto settings = ProjectSettings::defaults("Distribution test");
        settings["game"] = default_game_settings("forge.distribution-test", "Distribution test");
        settings["startup_scene"] = {{"asset", id},
                                     {"source", path_utf8(catalog.records().at(id).source)}};
        Json manifest{{"format", "forge.standalone"},
                      {"version", 1},
                      {"profile", "development"},
                      {"executable", "forge_game"},
                      {"content", "content"},
                      {"target", {{"platform", "linux"}, {"backend", "none"}}},
                      {"engine",
                       {{"profile", "static-core"},
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
        {
            ProjectLease lease(project);
            auto result = export_standalone_game(lease, request);
            check(result.at("ok") && std::filesystem::exists(exported / "forge_game"),
                  "Export assembly failed");
            const auto first = asset_storage::read(exported / "forge.standalone.json");
            project_settings["ecs_workers"] = 4;
            ProjectSettings(project).save(project_settings);
            reject([&] { export_standalone_game(lease, request); });
            check(asset_storage::read(exported / "forge.standalone.json") == first,
                  "An unsupported worker profile replaced the last good export");
            runtime["engine"]["runtime_ecs_workers"] = runtime_ecs_worker_profile_version;
            asset_storage::replace(kit / "forge.runtime-kit.json", runtime.dump());
            (void)export_standalone_game(lease, request);
            const auto worker_export =
                open_standalone_distribution(exported, target, "static-core", std::string(64, 'a'));
            check(worker_export.settings.at("ecs_workers") == 4,
                  "Worker export dropped its saved execution configuration");
            const auto relocated_workers = scratch / "relocated-worker-export";
            std::filesystem::copy(exported, relocated_workers,
                                  std::filesystem::copy_options::recursive);
            check(open_standalone_distribution(relocated_workers, target, "static-core",
                                               std::string(64, 'a'))
                          .settings.at("ecs_workers") == 4,
                  "Relocated worker distribution lost its execution configuration");
            project_settings["ecs_workers"] = 1;
            ProjectSettings(project).save(project_settings);
            runtime["engine"].erase("runtime_ecs_workers");
            asset_storage::replace(kit / "forge.runtime-kit.json", runtime.dump());
            (void)export_standalone_game(lease, request);
            check(asset_storage::read(exported / "forge.standalone.json") == first,
                  "Serial export did not retain its existing manifest shape");
            std::filesystem::create_directories(project / "Native");
            asset_storage::replace(project / "Native/gameplay.cpp",
                                   "// Retired movement-only source remains on disk.\n");
            reject([&] { export_standalone_game(lease, request); });
            check(asset_storage::read(exported / "forge.standalone.json") == first,
                  "Unsupported legacy source replaced the good export");
            std::filesystem::remove(project / "Native/gameplay.cpp");
            for (unsigned boundary = 0; boundary <= 5; ++boundary) {
                std::stop_source stop;
                reject([&] {
                    export_standalone_game(
                        lease, request, {},
                        [&](const GameExportProgress& p) {
                            if (p.completed == boundary)
                                stop.request_stop();
                        },
                        stop.get_token());
                });
                check(asset_storage::read(exported / "forge.standalone.json") == first,
                      "Cancelled export changed last good");
            }
            for (const auto& entry : std::filesystem::directory_iterator(scratch))
                if (path_utf8(entry.path().filename()).starts_with(".forge-export-"))
                    for (const auto& staged : std::filesystem::directory_iterator(entry.path()))
                        check(!path_utf8(staged.path().filename()).starts_with("work-") &&
                                  staged.path().filename() != "transaction.json",
                              "Cancelled export left owned scratch or a promotion journal");
            asset_storage::replace(kit / "forge_game", "corrupt");
            reject([&] { export_standalone_game(lease, request); });
            check(asset_storage::read(exported / "forge.standalone.json") == first,
                  "Invalid kit changed last good");
            asset_storage::replace(kit / "forge_game", "test executable bytes");
            project_settings["game"]["title"] = "Replacement";
            ProjectSettings(project).save(project_settings);
            // Use actual validated game setting field instead of accepting unknown keys.
            result = export_standalone_game(lease, request);
            (void)open_standalone_distribution(exported, target, "static-core",
                                               std::string(64, 'a'));
            const auto destination_name = path_utf8(exported.filename());
            const auto control = exported.parent_path() /
                                 (".forge-export-" + asset_detail::content_digest(
                                                         std::as_bytes(std::span(destination_name)))
                                                         .substr(0, 24));
            const auto committed = asset_storage::read(exported / "forge.standalone.json");
            {
                GameTestLease run(exported);
                reject([&] { recover_game_export(exported); });
                reject([&] { export_standalone_game(lease, request); });
                check(asset_storage::read(exported / "forge.standalone.json") == committed,
                      "A running game test allowed export replacement");
                run.finish(true);
            }
            check(!std::filesystem::exists(control / "game-test.json"),
                  "Confirmed game retirement retained its marker");
            recover_game_export(exported);
            for (const auto explicit_finish : {false, true}) {
                {
                    GameTestLease interrupted(exported);
                    if (explicit_finish)
                        interrupted.finish(false);
                }
                reject([&] { recover_game_export(exported); });
                reject([&] { export_standalone_game(lease, request); });
                reject([&] { GameTestLease retry(exported); });
                check(asset_storage::read(exported / "forge.standalone.json") == committed &&
                          std::filesystem::is_regular_file(control / "game-test.json"),
                      "Unconfirmed game test output was replaced or its marker lost");
                // Only this test's explicitly created marker, after its fixture
                // has proved the blocked restart. No product recovery heuristic.
                std::filesystem::remove(control / "game-test.json");
            }
            {
                GameTestLease changed(exported);
                asset_storage::replace(control / "game-test.json", "external change");
                reject([&] { changed.finish(true); });
            }
            check(asset_storage::read(control / "game-test.json") == "external change",
                  "Game completion erased a changed ownership record");
            std::filesystem::remove(control / "game-test.json");
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

            // Exercise the actual recovery owner at every promotion checkpoint.
            // The two manifests differ, so retaining the wrong generation fails.
            check(first != committed, "Recovery fixture needs distinct generations");
            auto clone = [&](const std::filesystem::path& path, const std::string& text) {
                std::filesystem::copy(exported, path, std::filesystem::copy_options::recursive);
                asset_storage::replace(path / "forge.standalone.json", text);
            };
            auto control_for = [](const std::filesystem::path& path) {
                const auto name = path_utf8(path.filename());
                return path.parent_path() /
                       (".forge-export-" +
                        asset_detail::content_digest(std::as_bytes(std::span(name))).substr(0, 24));
            };
            const auto old_hash = asset_detail::content_digest(std::as_bytes(std::span(*first)));
            auto journal_for = [&](const std::filesystem::path& path, bool had_previous) {
                return Json{{"version", 1},
                            {"destination", path_utf8(path)},
                            {"candidate", hash},
                            {"previous", had_previous ? old_hash : ""}};
            };
            struct Checkpoint {
                const char* name;
                bool had_previous, candidate, previous, promoted;
            };
            for (const auto& point : {Checkpoint{"journal", true, false, false, false},
                                      Checkpoint{"candidate", true, true, false, false},
                                      Checkpoint{"previous", true, true, true, false},
                                      Checkpoint{"promoted", true, false, true, true},
                                      Checkpoint{"first-journal", false, false, false, false},
                                      Checkpoint{"first-candidate", false, true, false, false},
                                      Checkpoint{"first-promoted", false, false, false, true}}) {
                const auto destination = scratch / point.name;
                const auto journal_root = control_for(destination);
                std::filesystem::create_directory(journal_root);
                if (point.promoted)
                    clone(destination, *committed);
                else if (point.had_previous && !point.previous)
                    clone(destination, *first);
                if (point.candidate)
                    clone(journal_root / "candidate", *committed);
                if (point.previous)
                    clone(journal_root / "previous", *first);
                asset_storage::replace(journal_root / "transaction.json",
                                       journal_for(destination, point.had_previous).dump());
                // Unjournaled scratch is not owned by recovery.
                std::filesystem::create_directory(journal_root / "work-unrecognized");
                asset_storage::replace(journal_root / "work-unrecognized/user.txt", "keep");
                recover_game_export(destination);
                recover_game_export(destination);
                if (point.had_previous || point.promoted)
                    check(asset_storage::read(destination / "forge.standalone.json") ==
                              (point.promoted ? committed : first),
                          "Recovery retained the wrong generation");
                else
                    check(!std::filesystem::exists(destination),
                          "Recovery published an uncommitted first export");
                check(!std::filesystem::exists(journal_root / "transaction.json") &&
                          !std::filesystem::exists(journal_root / "candidate") &&
                          !std::filesystem::exists(journal_root / "previous") &&
                          asset_storage::read(journal_root / "work-unrecognized/user.txt") ==
                              "keep",
                      "Recovery left its transaction or removed unrelated scratch");
            }

            // Recovery must reject redirected roots before any restore/cleanup.
            for (const char* slot : {"previous", "candidate"}) {
                const auto destination = scratch / (std::string("redirected-") + slot);
                const auto journal_root = control_for(destination);
                const auto external = scratch / (std::string("external-") + slot);
                std::filesystem::create_directory(journal_root);
                clone(external, slot == std::string_view("previous") ? *first : *committed);
                std::error_code link_error;
                std::filesystem::create_directory_symlink(external, journal_root / slot,
                                                          link_error);
                if (link_error) {
                    std::cout << "SKIP recovery directory-link fixture: " << link_error.message()
                              << '\n';
                    continue;
                }
                const auto other = slot == std::string_view("previous") ? "candidate" : "previous";
                clone(journal_root / other,
                      slot == std::string_view("previous") ? *committed : *first);
                const auto journal = journal_for(destination, true).dump();
                asset_storage::replace(journal_root / "transaction.json", journal);
                reject([&] { recover_game_export(destination); },
                       "Recovery admitted a redirected staging root");
                check(!std::filesystem::exists(destination) &&
                          std::filesystem::is_symlink(journal_root / slot) &&
                          std::filesystem::exists(journal_root / other) &&
                          asset_storage::read(journal_root / "transaction.json") == journal,
                      "Recovery changed redirected transaction paths");
                (void)verify_distribution_files(external, "forge.standalone.json",
                                                "forge.standalone");
            }
            // Unknown or modified bytes must survive a refused recovery.
            for (const char* slot : {"previous", "candidate", "destination"}) {
                const auto destination = scratch / (std::string("modified-") + slot);
                const auto journal_root = control_for(destination);
                std::filesystem::create_directory(journal_root);
                clone(destination, *committed);
                clone(journal_root / "previous", *first);
                clone(journal_root / "candidate", *committed);
                const auto edited =
                    slot == std::string_view("destination") ? destination : journal_root / slot;
                const auto journal = journal_for(destination, true).dump();
                asset_storage::replace(journal_root / "transaction.json", journal);
                for (const char* file : {"user.txt", "forge_game"}) {
                    asset_storage::replace(edited / file, "user changes");
                    reject([&] { recover_game_export(destination); },
                           "Recovery admitted modified distribution contents");
                    check(asset_storage::read(edited / file) == "user changes" &&
                              asset_storage::read(journal_root / "transaction.json") == journal &&
                              asset_storage::read(destination / "forge.standalone.json") ==
                                  committed,
                          "Recovery changed modified data or the committed output");
                    if (file == std::string_view("user.txt"))
                        std::filesystem::remove(edited / file);
                }
            }
            std::filesystem::create_directory(scratch / "unrelated");
            asset_storage::replace(scratch / "unrelated/user.txt", "keep");
            request.destination = scratch / "unrelated";
            reject([&] { export_standalone_game(lease, request); });
            check(asset_storage::read(scratch / "unrelated/user.txt") == "keep",
                  "Export overwrote unrelated directory");
            std::error_code link_error;
            std::filesystem::create_directory_symlink(exported, scratch / "linked-output",
                                                      link_error);
            if (!link_error) {
                request.destination = scratch / "linked-output";
                reject([&] { export_standalone_game(lease, request); });
                check(asset_storage::read(exported / "forge.standalone.json") == committed,
                      "Symbolic-link export changed its target");
            }
            if (argc == 5) {
                const auto library = std::filesystem::absolute(argv[3]);
                const auto native = project / "Native" / library.filename();
                const auto modulekit = scratch / "module-kit";
                std::filesystem::create_directories(native.parent_path());
                std::filesystem::create_directories(modulekit);
                std::filesystem::copy_file(library, native);
                std::filesystem::copy_file(library, modulekit / library.filename());
                const auto binary = asset_detail::read_bytes(library, 512ull * 1024 * 1024);
                Json deployment{{"format", "forge.module-kit"},
                                {"version", 1},
                                {"library", path_utf8(library.filename())},
                                {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                                {"target", runtime.at("target")},
                                {"files",
                                 {{path_utf8(library.filename()),
                                   {{"bytes", binary.size()},
                                    {"sha256", asset_detail::content_digest(binary)}}}}}};
                asset_storage::replace(modulekit / "forge.module-kit.json", deployment.dump());
                runtime["engine"]["profile"] = "shared-native-sdk";
                runtime["engine"]["sdk_fingerprint"] = FORGE_NATIVE_SDK_FINGERPRINT;
                asset_storage::replace(kit / "forge.runtime-kit.json", runtime.dump());
                project_settings["modules"] =
                    Json::array({{{"id", "project.sdk_probe"},
                                  {"sdk", "experimental-1"},
                                  {"implementation", "1"},
                                  {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
                                  {"library", path_utf8(native.lexically_relative(project))},
                                  {"dependencies", {"forge.input", "forge.transforms"}}}});
                ProjectSettings(project).save(project_settings);
                request.destination = exported;
                request.inspection_runtime = std::filesystem::absolute(argv[4]);
                request.module_kits = {{"project.sdk_probe", modulekit}};
                result = export_standalone_game(lease, request);
                check(result.at("manifest")
                          .at("settings")
                          .at("modules")[0]
                          .at("library")
                          .get<std::string>()
                          .starts_with("native/"),
                      "Native module path not relocated");
                const auto good = asset_storage::read(exported / "forge.standalone.json");
                for (unsigned mismatch = 0; mismatch < 3; ++mismatch) {
                    auto altered = result.at("manifest");
                    if (mismatch == 0)
                        altered["native_modules"] = Json::array();
                    else if (mismatch == 1)
                        altered["native_modules"][0]["deployment"]["fingerprint"] =
                            std::string(64, '0');
                    else {
                        auto& files = altered["native_modules"][0]["deployment"]["files"];
                        files.begin().value()["sha256"] = std::string(64, '0');
                    }
                    asset_storage::replace(exported / "forge.standalone.json", altered.dump());
                    reject([&] {
                        open_standalone_distribution(exported, target, "shared-native-sdk",
                                                     FORGE_NATIVE_SDK_FINGERPRINT);
                    });
                }
                asset_storage::replace(exported / "forge.standalone.json", good.value());
                reject([&] {
                    export_standalone_game(lease, request, {}, [&](const GameExportProgress& p) {
                        if (p.completed == 3)
                            asset_storage::replace(native, "changed module after inspection");
                    });
                });
                check(asset_storage::read(exported / "forge.standalone.json") == good,
                      "Changed source module replaced good export");
                asset_storage::replace(
                    native, {reinterpret_cast<const char*>(binary.data()), binary.size()});
                deployment["fingerprint"] = std::string(64, '0');
                asset_storage::replace(modulekit / "forge.module-kit.json", deployment.dump());
                reject([&] { export_standalone_game(lease, request); });
                check(asset_storage::read(exported / "forge.standalone.json") == good,
                      "Incompatible module kit replaced good export");
            }
        } // Release the Windows project writer handle before moving its directory.
        std::filesystem::rename(project, scratch / "unavailable");
        auto open = [&] {
            return open_standalone_distribution(out, target, "static-core", std::string(64, 'a'));
        };
        auto admitted = open();
        check(admitted.settings == settings && admitted.content == out / "content",
              "Admitted configuration changed");
        ProjectSettings runtime_settings(admitted.content, admitted.settings);
        reject([&] { runtime_settings.save(settings); });
        check(!std::filesystem::exists(out / "content/forge.project.json"),
              "Runtime created source project configuration");
        reject([&] {
            open_standalone_distribution(out, {"windows", "d3d12"}, "static-core",
                                         std::string(64, 'a'));
        });
        reject([&] {
            open_standalone_distribution(out, target, "shared-native-sdk", std::string(64, 'a'));
        });
        reject([&] {
            open_standalone_distribution(out, target, "static-core", std::string(64, 'c'));
        });
        for (unsigned test = 0; test < 8; ++test) {
            auto bad = manifest;
            if (test == 0)
                bad["settings"]["startup_scene"] = nullptr;
            if (test == 1)
                bad["assets"][0]["revision"] = std::string(64, 'f');
            if (test == 2)
                bad["content"] = "../source";
            if (test == 3)
                bad["files"]["forge_game"]["bytes"] = 0u;
            if (test == 4)
                bad["profile"] = "shipping";
            if (test == 5)
                bad["settings"]["game"]["profile"] = "shipping";
            if (test == 6)
                bad["executable"] = "absent.exe";
            if (test == 7)
                bad["native_modules"] = Json::array({{{"id", "orphan"}}});
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
