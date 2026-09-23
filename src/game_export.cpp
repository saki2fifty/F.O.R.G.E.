#include "game_export.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "authored_inspection.hpp"
#include "bounded_json.hpp"
#include "publish_directory.hpp"
#include "standalone_manifest.hpp"
#include <forge/project.hpp>
#include <fstream>
namespace forge {
namespace {
using Json = nlohmann::json;
void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error("game.export: " + message);
}
void cancel(std::stop_token token) {
    if (token.stop_requested())
        throw std::runtime_error(
            "game.export.cancelled: Export cancelled; previous output retained");
}
std::string digest(const std::filesystem::path& path) {
    return asset_detail::content_digest(asset_detail::read_bytes(path, 16 * 1024 * 1024));
}
struct Output {
    std::filesystem::path destination, control;
    std::unique_ptr<ProjectLease> lock;
    explicit Output(const std::filesystem::path& requested) {
        destination = std::filesystem::absolute(requested).lexically_normal();
        require(!destination.filename().empty() && destination.has_parent_path(),
                "Choose a named destination directory");
        asset_storage::ordinary(destination);
        require(std::filesystem::is_directory(destination.parent_path()),
                "Destination parent must exist");
        const auto name = path_utf8(destination.filename());
        control = destination.parent_path() /
                  (".forge-export-" +
                   asset_detail::content_digest(std::as_bytes(std::span(name))).substr(0, 24));
        asset_storage::ordinary(control);
        std::filesystem::create_directory(control);
        lock = std::make_unique<ProjectLease>(control);
    }
    void recover() {
        const auto text = asset_storage::read(control / "transaction.json", 65536);
        if (!text)
            return;
        auto journal =
            asset_detail::parse_bounded_json(std::as_bytes(std::span(*text)), 65536, 128, 8);
        require(journal.at("version") == 1 && journal.at("destination") == path_utf8(destination),
                "Export recovery journal does not match destination");
        const auto stage = control / "candidate", previous = control / "previous";
        const auto candidate_hash = journal.at("candidate").get<std::string>();
        const auto previous_hash = journal.at("previous").get<std::string>();
        if (std::filesystem::exists(destination)) {
            const auto current = digest(destination / "forge.standalone.json");
            require(current == candidate_hash || current == previous_hash,
                    "Destination changed outside export; recovery preserved all files");
            (void)verify_distribution_files(destination, "forge.standalone.json",
                                            "forge.standalone");
        } else if (std::filesystem::exists(previous)) {
            require(digest(previous / "forge.standalone.json") == previous_hash,
                    "Previous export changed; recovery preserved it");
            (void)verify_distribution_files(previous, "forge.standalone.json", "forge.standalone");
            asset_detail::rename_new_directory(previous, destination);
            asset_storage::sync_directory(destination.parent_path());
        }
        if (std::filesystem::exists(previous)) {
            require(digest(previous / "forge.standalone.json") == previous_hash,
                    "Previous export changed; refusing cleanup");
            (void)verify_distribution_files(previous, "forge.standalone.json", "forge.standalone");
            std::filesystem::remove_all(previous);
        }
        if (std::filesystem::exists(stage)) {
            require(digest(stage / "forge.standalone.json") == candidate_hash,
                    "Candidate changed; recovery preserved it");
            (void)verify_distribution_files(stage, "forge.standalone.json", "forge.standalone");
            std::filesystem::remove_all(stage);
        }
        asset_storage::erase_file(control / "transaction.json");
    }
};
bool contains(const std::filesystem::path& parent, const std::filesystem::path& child) {
    const auto relative = child.lexically_relative(parent);
    return !relative.empty() && *relative.begin() != "..";
}
void separate(const std::filesystem::path& output, const std::filesystem::path& source) {
    require(!contains(source, output) && !contains(output, source),
            "Export destination and source/kit folders must be separate");
}
void copy_checked(const ProjectPaths& from, const std::filesystem::path& locator,
                  const ProjectPaths& to, const std::filesystem::path& destination,
                  const Json& info, std::stop_token stop) {
    cancel(stop);
    const auto bytes = asset_detail::read_bytes(from.resolve(locator), 512ull * 1024 * 1024);
    require(info.at("bytes") == bytes.size() &&
                info.at("sha256") == asset_detail::content_digest(bytes),
            "Runtime kit changed during copy: " + path_utf8(locator));
    const auto path = to.resolve(destination);
    if (std::filesystem::exists(path)) {
        require(asset_detail::content_digest(
                    asset_detail::read_bytes(path, 512ull * 1024 * 1024)) == info.at("sha256"),
                "Conflicting runtime DLL/file: " + path_utf8(destination));
        return;
    }
    std::filesystem::create_directories(path.parent_path());
    asset_storage::replace(path, {reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
Json inventory(const std::filesystem::path& root, std::stop_token stop) {
    Json files = Json::object();
    std::uint64_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        cancel(stop);
        const auto status = entry.symlink_status();
        require(!std::filesystem::is_symlink(status), "Output contains a link");
        if (std::filesystem::is_directory(status))
            continue;
        require(std::filesystem::is_regular_file(status) && files.size() < 65536,
                "Output file budget/type invalid");
        const auto bytes = asset_detail::read_bytes(entry.path(), 512ull * 1024 * 1024);
        total += bytes.size();
        require(total <= 4ull * 1024 * 1024 * 1024, "Output exceeds 4 GiB development budget");
        files[path_utf8(entry.path().lexically_relative(root))] = {
            {"bytes", bytes.size()}, {"sha256", asset_detail::content_digest(bytes)}};
    }
    return files;
}
} // namespace
void recover_game_export(const std::filesystem::path& destination) {
    Output(destination).recover();
}
Json export_standalone_game(const ProjectLease& lease, const GameExportRequest& request,
                            const RuntimeUiInspector& inspect, GameExportObserver observer,
                            std::stop_token stop) {
    auto progress = [&](std::string stage, unsigned completed) {
        cancel(stop);
        if (observer)
            observer({std::move(stage), completed, 6});
        cancel(stop);
    };
    lease.check();
    progress("Validate project and runtime kit", 0);
    const ProjectPaths source(lease.root()), kit(request.runtime_kit);
    Output output(request.destination);
    separate(output.destination, source.root());
    separate(output.destination, kit.root());
    output.recover();
    // A destination belongs to this exporter only after complete admission.
    std::string previous_hash;
    if (std::filesystem::exists(output.destination)) {
        (void)verify_distribution_files(output.destination, "forge.standalone.json",
                                        "forge.standalone", stop);
        previous_hash = digest(output.destination / "forge.standalone.json");
    }
    auto settings = ProjectSettings(source.root()).document();
    const auto original_settings = settings;
    // Only runtime-owned configuration crosses the distribution boundary.
    // Machine tools, editor/plugin settings and arbitrary project extensions do not.
    settings = Json::object();
    for (const char* field : {"version", "name", "simulation_hz", "physics", "input",
                              "startup_scene", "game", "modules"})
        if (original_settings.contains(field))
            settings[field] = original_settings.at(field);
    require(!settings.contains("game") || settings.at("game").at("profile") == "development",
            "Only Development standalone export is currently supported");
    require(settings.contains("game") && !settings.at("startup_scene").is_null(),
            "Set Game defaults and Startup Scene in Project Settings first");
    require(!request.reference_schema.is_null(),
            "An owner-thread reflected schema snapshot is required");
    const auto runtime =
        verify_distribution_files(kit.root(), "forge.runtime-kit.json", "forge.runtime-kit", stop);
    const auto engine = runtime.at("engine");
    const RuntimePackageTarget target{runtime.at("target").at("platform"),
                                      runtime.at("target").at("backend")};
    const auto profile = engine.at("profile").get<std::string>(),
               fingerprint = engine.at("sdk_fingerprint").get<std::string>();
    const auto executable = ProjectPaths::normalize(
        std::filesystem::u8path(runtime.at("executable").get<std::string>()));
    require(executable.parent_path().empty() && runtime.at("files").contains(path_utf8(executable)),
            "Runtime kit executable missing");
    require(profile == "static-abi1" || profile == "shared-native-sdk",
            "Unsupported runtime linkage profile");
    require(runtime.at("files").contains("resources/ui/LatoLatin-Regular.ttf"),
            "Runtime kit default font missing");
    std::size_t native_count = 0;
    std::map<std::filesystem::path, std::string> inspected_binaries;
    for (const auto& module : settings.value("modules", Json::array()))
        if (module.is_object()) {
            ++native_count;
            const auto path =
                source.resolve(std::filesystem::u8path(module.at("library").get<std::string>()));
            inspected_binaries.emplace(path, asset_detail::content_digest(asset_detail::read_bytes(
                                                 path, 512ull * 1024 * 1024)));
        }
    require(native_count == request.module_kits.size(),
            "Supply one deployment kit per configured native module");
    auto reference_schema = request.reference_schema;
    if (native_count) {
        require(profile == "shared-native-sdk",
                "Native gameplay modules require the shared SDK runtime kit");
        require(!request.inspection_runtime.empty(), "Matching SDK inspection worker required");
        // Native code stays in the disposable worker, never the calling editor.
        reference_schema = detail::inspect_project_authoring(request.inspection_runtime,
                                                             source.root(), fingerprint, stop)
                               .at("reference_schema");
        require(reference_schema.is_object(), "Invalid native reference schema");
    }
    const auto startup = settings.at("startup_scene").at("asset").get<AssetId>();
    const std::array roots{startup};
    progress("Prepare required content metadata", 1);
    prepare_runtime_content_catalog(lease, roots, inspect, reference_schema, stop);
    const auto catalog_before = AssetCatalog::open_project(source.root()).document();
    // Unique work directories cannot be mistaken for a promoted distribution.
    const auto scratch = output.control / ("work-" + AssetId::generate().str());
    require(std::filesystem::create_directory(scratch), "Cannot reserve export staging");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code e;
            if (!path.empty())
                std::filesystem::remove_all(path, e);
        }
    } cleanup{scratch};
    ProjectPaths staging(scratch);
    progress("Package declared runtime content", 2);
    package_runtime_content(source.root(), scratch / "content", roots, target, {}, stop,
                            reference_schema, inspect);
    const auto content = open_runtime_content(scratch / "content", target, {}, stop);
    settings["startup_scene"]["source"] = path_utf8(content.records().at(startup).source);
    progress("Collect runtime and native modules", 3);
    for (const auto& [file, info] : runtime.at("files").items()) {
        const auto locator = ProjectPaths::normalize(std::filesystem::u8path(file));
        require(*locator.begin() != "content" && file != "forge.standalone.json",
                "Reserved runtime-kit path");
        copy_checked(kit, locator, staging, locator, info, stop);
    }
    Json module_inventory = Json::array();
    if (!settings.contains("modules"))
        settings["modules"] = Json::array();
    for (auto& module : settings["modules"]) {
        if (!module.is_object())
            continue; // Validated engine built-ins already linked into host.
        const auto id = module.at("id").get<std::string>();
        const ProjectPaths module_root(request.module_kits.at(id));
        separate(output.destination, module_root.root());
        const auto deployment = verify_distribution_files(
            module_root.root(), "forge.module-kit.json", "forge.module-kit", stop);
        require(deployment.at("fingerprint") == fingerprint &&
                    module.at("fingerprint") == fingerprint &&
                    deployment.at("target") == runtime.at("target"),
                "Native module profile/backend mismatch: " + id);
        const auto library = ProjectPaths::normalize(
            std::filesystem::u8path(deployment.at("library").get<std::string>()));
        require(library.parent_path().empty() &&
                    deployment.at("files").contains(path_utf8(library)),
                "Module kit library missing");
        const auto original_library =
            source.resolve(std::filesystem::u8path(module.at("library").get<std::string>()));
        require(inspected_binaries.at(original_library) ==
                    deployment.at("files").at(path_utf8(library)).at("sha256"),
                "Module deployment is stale: " + id);
        const auto runtime_library = std::filesystem::path("native") / id / library;
        for (const auto& [file, info] : deployment.at("files").items()) {
            const auto locator = ProjectPaths::normalize(std::filesystem::u8path(file));
            require(locator.parent_path().empty(), "Module kits contain native files only");
            copy_checked(module_root, locator, staging,
                         locator == library ? runtime_library : locator, info, stop);
        }
        module["library"] = path_utf8(runtime_library);
        module_inventory.push_back({{"id", id}, {"deployment", deployment}});
    }
    progress("Validate standalone distribution", 4);
    Json manifest{{"format", "forge.standalone"},
                  {"version", 1},
                  {"profile", "development"},
                  {"content", "content"},
                  {"target", runtime.at("target")},
                  {"engine", engine},
                  {"executable", path_utf8(executable)},
                  {"settings", settings},
                  {"assets", runtime_asset_inventory(content)},
                  {"native_modules", module_inventory},
                  {"files", inventory(scratch, stop)}};
    asset_storage::replace(scratch / "forge.standalone.json", manifest.dump(2));
    (void)open_standalone_distribution(scratch, target, profile, fingerprint, stop);
    lease.check();
    require(ProjectSettings(source.root()).document() == original_settings &&
                AssetCatalog::open_project(source.root()).document() == catalog_before,
            "Project changed during export; retry");
    for (const auto& [path, expected] : inspected_binaries)
        require(asset_detail::content_digest(
                    asset_detail::read_bytes(path, 512ull * 1024 * 1024)) == expected,
                "Native module changed during export; rebuild/retry");
    progress("Publish validated game", 5);
    // Final cancellation boundary. After the durable journal, complete or recover
    // the two directory renames; never report cancellation halfway through commit.
    const auto candidate = output.control / "candidate", previous = output.control / "previous";
    require(!std::filesystem::exists(candidate) && !std::filesystem::exists(previous),
            "Unrecovered export staging exists");
    asset_storage::replace(output.control / "transaction.json",
                           Json{{"version", 1},
                                {"destination", path_utf8(output.destination)},
                                {"candidate", digest(scratch / "forge.standalone.json")},
                                {"previous", previous_hash}}
                               .dump());
    try {
        asset_detail::rename_new_directory(scratch, candidate);
        cleanup.path.clear();
        if (!previous_hash.empty()) {
            require(digest(output.destination / "forge.standalone.json") == previous_hash,
                    "Previous export changed before promotion");
            asset_detail::rename_new_directory(output.destination, previous);
        }
        asset_detail::rename_new_directory(candidate, output.destination);
        asset_storage::sync_directory(output.destination.parent_path());
    } catch (...) {
        output.recover();
        throw;
    }
    // A cleanup failure must not misreport a successfully committed export.
    std::string warning;
    try {
        output.recover();
    } catch (const std::exception& e) {
        warning = e.what();
    }
    if (observer) {
        try {
            observer({"Export complete", 6, 6});
        } catch (...) {
        }
    }
    return {{"ok", true},
            {"destination", path_utf8(output.destination)},
            {"manifest", manifest},
            {"cleanup_warning", warning}};
}
} // namespace forge
