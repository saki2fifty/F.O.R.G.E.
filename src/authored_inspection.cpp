#include "authored_inspection.hpp"
#include "asset_bytes.hpp"
#include "asset_worker.hpp"
#include "authored_schema.hpp"
#include "bounded_json.hpp"
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
} // namespace
Json inspect_project_authoring(const std::filesystem::path& runtime,
                               const std::filesystem::path& project, const std::string& expected,
                               std::stop_token stop) {
    if (expected.size() != 64 ||
        expected.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Schema inspection requires the matching SDK fingerprint");
    if (stop.stop_requested())
        throw std::runtime_error("Schema inspection cancelled");
    const ProjectPaths paths(project);
    Staging work(paths.root());
    const auto settings_before = ProjectSettings(paths.root()).document();
    write(work.path / "request.json",
          {{"project", path_utf8(paths.root())}, {"fingerprint", expected}}, 65536);
    asset_detail::WorkerLimits limits;
    limits.file_bytes = output_limit;
    limits.total_bytes = output_limit + 65536;
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
    auto result = read(work.path / "result.json", output_limit);
    if (stop.stop_requested())
        throw std::runtime_error("Schema inspection cancelled");
    if (settings_before != ProjectSettings(paths.root()).document())
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
        const auto request = read(staging / "request.json", 65536);
        if (request.at("fingerprint") != FORGE_NATIVE_SDK_FINGERPRINT)
            throw std::runtime_error("Schema inspection SDK fingerprint mismatch");
        const auto root = std::filesystem::u8path(request.at("project").get<std::string>());
        if (!root.is_absolute())
            throw std::runtime_error("Schema project root must be absolute");
        write(staging / "result.json", export_project_authoring(root), output_limit);
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
