#include "authored_inspection.hpp"
#include "asset_bytes.hpp"
#include "asset_worker.hpp"
#include "authored_migration.hpp"
#include "authored_schema.hpp"
#include "bounded_json.hpp"
#include "json_value_equal.hpp"
#include <forge/native_sdk.hpp>
#include <forge/native_sdk_identity.h>
#include <forge/project.hpp>
#include <forge/project_paths.hpp>
#include <forge/world.hpp>
#include <fstream>
namespace forge::detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t output_limit = 17 * 1024 * 1024;
constexpr std::size_t migration_input_limit = 20 * 1024 * 1024;
constexpr std::size_t migration_output_limit = 33 * 1024 * 1024;
void ordinary(const std::filesystem::path& path) {
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
        std::filesystem::weakly_canonical(path) != path)
        throw std::runtime_error("Schema inspection staging must not redirect through links");
}
Json read(const std::filesystem::path& path, std::size_t limit) {
    ordinary(path);
    return asset_detail::parse_bounded_json(asset_detail::read_bytes(path, limit), limit, 1000000,
                                            32);
}
void write(const std::filesystem::path& path, const Json& value, std::size_t limit) {
    ordinary(path);
    if (std::filesystem::exists(path))
        throw std::runtime_error("Schema inspection output already exists");
    const auto bytes = value.dump();
    if (bytes.size() > limit)
        throw std::runtime_error("Schema inspection output exceeds bounds");
    std::ofstream out(path, std::ios::binary);
    if (!out.write(bytes.data(), std::streamsize(bytes.size())) || !out.flush())
        throw std::runtime_error("Cannot write schema inspection output");
    out.close();
    if (out.fail())
        throw std::runtime_error("Cannot close schema inspection output");
}
struct Staging {
    std::filesystem::path path;
    explicit Staging(const std::filesystem::path& project) {
        const ProjectPaths paths(project);
        const auto parent = paths.resolve(".forge/schema-jobs");
        ordinary(parent);
        std::filesystem::create_directories(parent);
        path = parent / AssetId::generate().str();
        if (!std::filesystem::create_directory(path))
            throw std::runtime_error("Cannot reserve schema inspection staging");
    }
    ~Staging() {
        std::error_code error;
        if (std::filesystem::weakly_canonical(path, error) == path && !error &&
            !std::filesystem::is_symlink(path, error))
            std::filesystem::remove_all(path, error);
    }
};
Json request_authoring(const std::filesystem::path& runtime, const std::filesystem::path& project,
                       const std::string& expected, const Json& migration, std::stop_token stop) {
    if (expected.size() != 64 ||
        expected.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Schema inspection requires the matching SDK fingerprint");
    if (stop.stop_requested())
        throw std::runtime_error("Schema inspection cancelled");
    const ProjectPaths paths(project);
    Staging work(paths.root());
    const auto settings_before = ProjectSettings(paths.root()).document();
    Json request{{"project", path_utf8(paths.root())}, {"fingerprint", expected}};
    const bool migrate = !migration.is_null();
    if (migrate)
        request["migration"] = migration;
    const auto input_bytes = migrate ? migration_input_limit : 65536;
    const auto output_bytes = migrate ? migration_output_limit : output_limit;
    write(work.path / "request.json", request, input_bytes);
    asset_detail::WorkerLimits limits;
    limits.file_bytes = output_bytes;
    limits.total_bytes = output_bytes + input_bytes;
    limits.files = 4;
    try {
        asset_detail::run_worker(asset_detail::WorkerKind::Schema, runtime, work.path, stop,
                                 limits);
    } catch (...) {
        if (!stop.stop_requested() && std::filesystem::is_regular_file(work.path / "error.json")) {
            const auto error = read(work.path / "error.json", 16384);
            throw std::runtime_error(error.at("message").get<std::string>());
        }
        throw;
    }
    auto result = read(work.path / "result.json", output_bytes);
    if (stop.stop_requested())
        throw std::runtime_error("Schema inspection cancelled");
    if (!json_value_equal(settings_before, ProjectSettings(paths.root()).document()))
        throw std::runtime_error("Project module settings changed during schema inspection");
    if (result.value("format", "") != "forge.authored-types" || result.at("version") != 1 ||
        result.at("fingerprint") != expected || result.at("profile") != "shared-native-sdk" ||
        !result.at("components").is_array() || result.at("components").size() > 256)
        throw std::runtime_error("Schema inspection returned an incompatible manifest");
    // Transport runs on a worker task. Native reconstruction/admission belongs
    // to the caller's owner thread: do not create/register another engine world
    // concurrently with editor Flecs access (C addons have process-global IDs).
    for (const auto& item : result.at("components")) {
        bool known_module = false;
        for (const auto& module : settings_before.value("modules", Json::array()))
            if (module.is_object() && module.at("id") == item.at("module"))
                known_module = true;
        if (!known_module)
            throw std::runtime_error("Schema owner is absent from project modules");
    }
    return result;
}
} // namespace
Json inspect_project_authoring(const std::filesystem::path& runtime,
                               const std::filesystem::path& project, const std::string& expected,
                               std::stop_token stop) {
    return request_authoring(runtime, project, expected, nullptr, stop);
}
Json migrate_project_authoring(const std::filesystem::path& runtime,
                               const std::filesystem::path& project, const std::string& expected,
                               const Json& source, const Json& target, const Json& rules,
                               const Json& values, std::stop_token stop) {
    if (!values.is_array() || values.empty() || values.size() > 4096)
        throw std::runtime_error("Choose1–4096 component values for one migration candidate");
    const auto result = request_authoring(
        runtime, project, expected,
        {{"source", source}, {"target", target}, {"rules", rules}, {"values", values}}, stop);
    const auto& candidate = result.at("migration");
    if (!json_value_equal(candidate.at("source"), source) ||
        !json_value_equal(candidate.at("target"), target) || !candidate.at("values").is_array() ||
        candidate.at("values").size() != values.size())
        throw std::runtime_error("Migration worker returned a mismatched candidate");
    for (std::size_t i = 0; i < values.size(); ++i)
        if (candidate.at("values")[i].at("property_intent") != values[i].at("property_intent"))
            throw std::runtime_error("Migration worker changed property-intent ownership");
    return candidate.at("values");
}
Json export_project_authoring(const std::filesystem::path& root) {
    if (std::string_view(FORGE_NATIVE_SDK_PROFILE) != "shared-native-sdk")
        throw std::runtime_error("Schema inspection requires the exact shared native SDK");
    ProjectSettings project(root);
    auto modules = project_native_modules(root, project.document());
    for (auto& module : modules) {
        module.runtime_roles = 0;
        // Schema-only inspection supplies no simulation providers. Original
        // descriptor permissions and all runtime activation checks stay intact.
        module.required_services &=
            capability(Capability::Diagnostics) | capability(Capability::Profiling);
    }
    EngineContext engine(WorldRole::Validation, false, std::move(modules));
    return {{"format", "forge.authored-types"},
            {"version", 1},
            {"fingerprint", FORGE_NATIVE_SDK_FINGERPRINT},
            {"profile", FORGE_NATIVE_SDK_PROFILE},
            {"components", export_authored_types(engine.world().world())}};
}
int authored_inspection_worker() {
    const auto staging = std::filesystem::current_path();
    try {
        if (std::string_view(FORGE_NATIVE_SDK_PROFILE) != "shared-native-sdk")
            throw std::runtime_error("Schema inspection requires the exact shared native SDK");
        const auto request = read(staging / "request.json", migration_input_limit);
        if (request.at("fingerprint") != FORGE_NATIVE_SDK_FINGERPRINT)
            throw std::runtime_error("Schema inspection SDK fingerprint mismatch");
        const auto root = std::filesystem::u8path(request.at("project").get<std::string>());
        if (!root.is_absolute())
            throw std::runtime_error("Schema project root must be absolute");
        auto result = export_project_authoring(root);
        if (request.contains("migration")) {
            const auto& operation = request.at("migration");
            const auto& source = operation.at("source");
            const auto& target = operation.at("target");
            const auto& values = operation.at("values");
            if (!values.is_array() || values.empty() || values.size() > 4096)
                throw std::runtime_error("Migration candidate count exceeds supported bounds");
            bool matched = false;
            for (const auto& declaration : result.at("components"))
                matched |= json_value_equal(declaration, target);
            if (!matched)
                throw std::runtime_error(
                    "Migration target differs from the current SDK module schema");
            // Project registration hooks have already retired with the export
            // world. Migration uses engine-owned reconstructed Meta exclusively.
            EngineContext values_world(WorldRole::Validation);
            AuthoredMigration migration(values_world.world().world(), source, target,
                                        operation.at("rules"));
            auto migrated = Json::array();
            std::size_t bytes = 0;
            for (const auto& item : values) {
                const bool partial = item.at("property_intent").get<bool>();
                auto candidate = migration.migrate(item.at("value"), partial);
                bytes += candidate.dump().size();
                if (bytes > 16 * 1024 * 1024)
                    throw std::runtime_error("Migrated value output exceeds16MiB");
                migrated.push_back({{"value", std::move(candidate)}, {"property_intent", partial}});
            }
            result["migration"] = {
                {"source", source}, {"target", target}, {"values", std::move(migrated)}};
        }
        write(staging / "result.json", result,
              request.contains("migration") ? migration_output_limit : output_limit);
        return 0;
    } catch (const std::exception& e) {
        try {
            write(staging / "error.json", {{"message", std::string(e.what()).substr(0, 8192)}},
                  16384);
        } catch (...) {
        }
        return 1;
    }
}
} // namespace forge::detail
