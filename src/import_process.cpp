#include "import_process.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <algorithm>
#include <fstream>

namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::size_t manifest_limit = 4 * 1024 * 1024;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void ordinary(const std::filesystem::path& path) {
    require(!std::filesystem::is_symlink(std::filesystem::symlink_status(path)) &&
                std::filesystem::weakly_canonical(path) == path,
            "Import staging must not redirect through filesystem links");
}
void name_valid(std::string_view name) {
    require(!name.empty() && name.size() <= 128 && name.front() != '.' && name != "manifest.json" &&
                std::all_of(name.begin(), name.end(),
                            [](char c) {
                                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                       c == '.' || c == '_' || c == '-';
                            }),
            "Invalid import snapshot/output filename");
    // Generated lowercase names avoid portable case aliases and device paths.
    const auto stem = name.substr(0, name.find('.'));
    const bool numbered_device = stem.size() == 4 &&
                                 (stem.starts_with("com") || stem.starts_with("lpt")) &&
                                 stem[3] >= '1' && stem[3] <= '9';
    require(stem != "con" && stem != "nul" && stem != "aux" && stem != "prn" && !numbered_device &&
                name.back() != '.',
            "Reserved import snapshot/output filename");
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    ordinary(path);
    require(!std::filesystem::exists(path), "Import worker refuses to overwrite staging files");
    std::ofstream out(path, std::ios::binary);
    require(bool(out.write(reinterpret_cast<const char*>(bytes.data()),
                           std::streamsize(bytes.size()))) &&
                bool(out.flush()),
            "Cannot write import staging file");
    out.close();
    require(!out.fail(), "Cannot close import staging file");
}
void write_json(const std::filesystem::path& path, const Json& value) {
    const auto text = value.dump();
    require(text.size() <= manifest_limit, "Import manifest exceeds byte limit");
    (void)parse_bounded_json(std::as_bytes(std::span(text)), manifest_limit);
    write(path, std::as_bytes(std::span(text)));
}
Json files_manifest(const std::vector<ArtifactFile>& files, WorkerLimits limits) {
    require(!files.empty() && files.size() < limits.files, "Import file count exceeds budget");
    std::set<std::string> names;
    std::uint64_t total = manifest_limit;
    require(total <= limits.total_bytes, "Import budget cannot hold completion manifest");
    Json result = Json::array();
    for (const auto& file : files) {
        name_valid(file.name);
        require(names.insert(file.name).second, "Duplicate import filename");
        require(file.bytes.size() <= limits.file_bytes &&
                    file.bytes.size() <= limits.total_bytes - total,
                "Import snapshot/output exceeds byte budget");
        total += file.bytes.size();
        result.push_back({{"name", file.name},
                          {"bytes", file.bytes.size()},
                          {"sha256", content_digest(file.bytes)}});
    }
    return result;
}
std::vector<ArtifactFile> read_files(const std::filesystem::path& directory, const Json& manifest,
                                     WorkerLimits limits, bool completion) {
    ordinary(directory);
    require(manifest.is_array() && !manifest.empty() && manifest.size() < limits.files,
            "Invalid import file manifest");
    std::set<std::string> names;
    std::vector<ArtifactFile> result;
    std::uint64_t total = manifest_limit;
    require(total <= limits.total_bytes, "Import budget cannot hold manifest");
    for (const auto& item : manifest) {
        const auto name = item.at("name").get<std::string>();
        name_valid(name);
        require(names.insert(name).second, "Duplicate import file manifest entry");
        require(item.at("bytes").is_number_unsigned(), "Invalid import file length");
        const auto count = item.at("bytes").get<std::uint64_t>();
        require(count <= limits.file_bytes && count <= limits.total_bytes - total,
                "Import output exceeds budget");
        const auto path = directory / name;
        ordinary(path);
        require(std::filesystem::is_regular_file(path), "Missing import file");
        auto bytes = read_bytes(path, std::size_t(count));
        require(bytes.size() == count &&
                    content_digest(bytes) == item.at("sha256").get<std::string>(),
                "Import file size/digest mismatch");
        total += count;
        result.push_back({name, std::move(bytes)});
    }
    if (completion)
        names.insert("manifest.json");
    for (const auto& file : std::filesystem::directory_iterator(directory))
        require(names.erase(file.path().filename().string()) == 1 && file.is_regular_file() &&
                    !file.is_symlink(),
                "Unexpected import output file");
    require(names.empty(), "Import manifest file missing");
    return result;
}
struct JobDirectory {
    std::filesystem::path path;
    explicit JobDirectory(const std::filesystem::path& project) {
        const ProjectPaths paths(project);
        const auto parent = paths.resolve(".forge/jobs");
        ordinary(parent);
        std::filesystem::create_directories(parent);
        path = parent / AssetId::generate().str();
        require(std::filesystem::create_directory(path), "Cannot reserve unique import staging");
    }
    ~JobDirectory() {
        std::error_code error;
        if (std::filesystem::weakly_canonical(path, error) == path && !error &&
            !std::filesystem::is_symlink(path, error))
            std::filesystem::remove_all(path, error);
    }
};
Json read_json(const std::filesystem::path& path, std::size_t limit = manifest_limit) {
    ordinary(path);
    return parse_bounded_json(read_bytes(path, limit), limit);
}
} // namespace
std::vector<ArtifactFile> run_import_process(const std::filesystem::path& executable,
                                             const std::filesystem::path& project,
                                             ImportProcessRequest request, WorkerLimits limits,
                                             std::stop_token stop) {
    require(!stop.stop_requested(), "Import cancelled before snapshot");
    auto manifest = files_manifest(request.inputs, limits);
    JobDirectory job(project);
    require(std::filesystem::create_directory(job.path / "input") &&
                std::filesystem::create_directory(job.path / "output"),
            "Cannot create import directories");
    for (const auto& file : request.inputs)
        write(job.path / "input" / file.name, file.bytes);
    write_json(job.path / "request.json", {{"format", "forge.import-worker"},
                                           {"version", 1},
                                           {"payload", request.payload},
                                           {"inputs", manifest}});
    request.inputs.clear();
    request.inputs.shrink_to_fit();
    try {
        run_worker(WorkerKind::Import, executable, job.path, stop, limits);
    } catch (const std::exception& e) {
        if (stop.stop_requested())
            throw;
        std::string diagnostic = e.what();
        try {
            const auto error = read_json(job.path / "error.json", 64 * 1024);
            if (error.at("format") == "forge.import-error" && error.at("version") == 1)
                diagnostic += "; " + error.at("code").get<std::string>() + ": " +
                              error.at("message").get<std::string>();
        } catch (...) { /* Preserve the supervisor error when no valid diagnostic exists. */
        }
        diagnostic.resize(std::min<std::size_t>(8192, diagnostic.size()));
        throw std::runtime_error(diagnostic);
    }
    const auto result = read_json(job.path / "output/manifest.json");
    require(result.at("format") == "forge.import-result" && result.at("version") == 1,
            "Unsupported import result manifest");
    require(!stop.stop_requested(), "Import cancelled before candidate validation");
    return read_files(job.path / "output", result.at("files"), limits, true);
}
ImportProcessRequest read_import_process_request(const std::filesystem::path& staging,
                                                 WorkerLimits limits) {
    const auto request = read_json(staging / "request.json");
    require(request.at("format") == "forge.import-worker" && request.at("version") == 1 &&
                request.at("payload").is_object(),
            "Unsupported import worker request");
    return {request.at("payload"),
            read_files(staging / "input", request.at("inputs"), limits, false)};
}
std::string read_import_process_recipe(const std::filesystem::path& staging) {
    const auto request = read_json(staging / "request.json");
    require(request.at("format") == "forge.import-worker" && request.at("version") == 1 &&
                request.at("payload").is_object(),
            "Unsupported import worker request");
    auto recipe = request.at("payload").at("recipe").get<std::string>();
    require(!recipe.empty() && recipe.size() <= 256, "Invalid import recipe identifier");
    return recipe;
}
void write_import_process_result(const std::filesystem::path& staging,
                                 const std::vector<ArtifactFile>& files, WorkerLimits limits) {
    auto manifest = files_manifest(files, limits);
    ordinary(staging / "output");
    require(std::filesystem::is_empty(staging / "output"), "Import output directory is not empty");
    for (const auto& file : files)
        write(staging / "output" / file.name, file.bytes);
    write_json(staging / "output/manifest.json",
               {{"format", "forge.import-result"}, {"version", 1}, {"files", std::move(manifest)}});
}
void write_import_process_error(const std::filesystem::path& staging, std::string_view code,
                                std::string_view message) noexcept {
    try {
        write_json(staging / "error.json", {{"format", "forge.import-error"},
                                            {"version", 1},
                                            {"code", code.substr(0, 128)},
                                            {"message", message.substr(0, 8192)}});
    } catch (...) {
    }
}
} // namespace forge::asset_detail
