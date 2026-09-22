#include "import_process.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "worker_stage_lease.hpp"
#include <algorithm>
#include <forge/gltf_accessors.hpp>
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
    std::unique_ptr<WorkerStageLease> owner;
    JobDirectory(const std::filesystem::path& project, const char* kind) {
        const ProjectPaths paths(project);
        const auto parent = paths.resolve(".forge/jobs");
        ordinary(parent);
        std::filesystem::create_directories(parent);
        path = parent / AssetId::generate().str();
        require(std::filesystem::create_directory(path), "Cannot reserve unique import staging");
        owner = std::make_unique<WorkerStageLease>(path / "owner.lock",
                                                   Json{{"format", "forge.import-job"},
                                                        {"version", 1},
                                                        {"job", path.filename().string()},
                                                        {"kind", kind}}
                                                       .dump());
    }
    ~JobDirectory() {
        owner.reset(); // The worker has already joined; release Windows deletion exclusion.
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
    JobDirectory job(project, "import");
    require(std::filesystem::create_directory(job.path / "input") &&
                std::filesystem::create_directory(job.path / "output"),
            "Cannot create import directories");
    write_json(job.path / "request.json", {{"format", "forge.import-worker"},
                                           {"version", 1},
                                           {"payload", request.payload},
                                           {"inputs", manifest}});
    for (const auto& file : request.inputs)
        write(job.path / "input" / file.name, file.bytes);
    request.inputs.clear();
    request.inputs.shrink_to_fit();
    try {
        run_worker(WorkerKind::Import, executable, job.path, stop, limits, job.owner.get());
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
std::vector<ArtifactFile> run_model_animation_process(const std::filesystem::path& executable,
                                                      const std::filesystem::path& project,
                                                      std::span<const ArtifactFile> inputs,
                                                      std::stop_token stop) {
    require(!stop.stop_requested(), "Model animation conversion cancelled");
    require(inputs.size() >= 2 && inputs.size() <= 3, "Invalid model converter input count");
    constexpr std::size_t limit = 16 * 1024 * 1024;
    std::map<std::string, std::span<const std::byte>> lookup;
    for (const auto& input : inputs) {
        require((input.name == "source.gltf" || input.name == "config.json" ||
                 input.name == "animation.bin") &&
                    input.bytes.size() <= limit && lookup.emplace(input.name, input.bytes).second,
                "Invalid model converter input file");
    }
    require(lookup.contains("source.gltf") && lookup.contains("config.json"),
            "Missing model converter inputs");
    const auto source = parse_bounded_json(lookup.at("source.gltf"), limit);
    const auto config = parse_bounded_json(lookup.at("config.json"), 64 * 1024);
    auto fields = [](const Json& object, std::initializer_list<std::string_view> allowed) {
        require(object.is_object(), "Invalid canonical converter object");
        for (const auto& [key, value] : object.items()) {
            (void)value;
            require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(),
                    "Unexpected canonical converter field");
        }
    };
    fields(source, {"asset", "nodes", "scenes", "scene", "buffers", "bufferViews", "accessors",
                    "animations"});
    fields(source.at("asset"), {"version"});
    require(source.at("asset").at("version") == "2.0" && source.at("scene") == 0,
            "Unsupported canonical converter source");
    const auto& nodes = source.at("nodes");
    const auto& scenes = source.at("scenes");
    const auto& animations = source.at("animations");
    require(nodes.is_array() && !nodes.empty() && nodes.size() <= 1024 && scenes.is_array() &&
                scenes.size() == 1 && animations.is_array() && animations.size() <= 64,
            "Canonical converter node/scene/clip count exceeds bounds");
    fields(scenes[0], {"nodes"});
    for (const auto& node : nodes)
        fields(node, {"name", "translation", "rotation", "scale", "children"});
    if (source.contains("buffers")) {
        const auto& buffers = source.at("buffers");
        require(buffers.is_array() && buffers.size() == 1 && lookup.contains("animation.bin"),
                "Invalid canonical converter buffer");
        fields(buffers[0], {"uri", "byteLength"});
        require(buffers[0].at("uri") == "animation.bin" &&
                    buffers[0].at("byteLength") == lookup.at("animation.bin").size(),
                "Canonical converter buffer path/length mismatch");
    } else
        require(!lookup.contains("animation.bin"), "Unexpected canonical converter binary file");
    for (const auto& view : source.at("bufferViews"))
        fields(view, {"buffer", "byteOffset", "byteLength"});
    for (const auto& accessor : source.at("accessors")) {
        fields(accessor, {"bufferView", "componentType", "count", "type", "min", "max"});
        require(accessor.at("componentType") == 5126,
                "Canonical converter accessor must contain floats");
    }
    fields(config, {"skeleton", "animations"});
    require(config.at("skeleton") == Json{{"filename", "skeleton.ozz"}} &&
                config.at("animations").is_array() &&
                config.at("animations").size() == animations.size(),
            "Invalid canonical converter configuration");
    std::set<std::string> expected;
    for (const auto& input : inputs)
        expected.insert(input.name);
    expected.insert("skeleton.ozz");
    std::vector<std::string> outputs{"skeleton.ozz"};
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const auto name = "forge_clip_" + std::to_string(i),
                   filename = "clip-" + std::to_string(i) + ".ozz";
        const auto& animation = animations[i];
        const auto& settings = config.at("animations")[i];
        fields(animation, {"name", "samplers", "channels"});
        fields(settings, {"clip", "filename", "iframe_interval", "raw", "additive", "optimize",
                          "sampling_rate"});
        require(animation.at("name") == name && settings.at("clip") == name &&
                    settings.at("filename") == filename && settings.at("iframe_interval") == 0 &&
                    settings.at("raw") == false && settings.at("additive") == false &&
                    settings.at("optimize").is_boolean() &&
                    settings.at("sampling_rate").is_number_unsigned(),
                "Invalid canonical converter clip settings");
        const auto rate = settings.at("sampling_rate").get<std::uint64_t>();
        require(rate >= 1 && rate <= 240, "Invalid model animation sampling rate");
        require(animation.at("samplers").is_array() && animation.at("samplers").size() <= 3072 &&
                    animation.at("channels").is_array() && animation.at("channels").size() <= 3072,
                "Canonical converter channel count exceeds bounds");
        for (const auto& sampler : animation.at("samplers"))
            fields(sampler, {"input", "output", "interpolation"});
        for (const auto& channel : animation.at("channels")) {
            fields(channel, {"sampler", "target"});
            fields(channel.at("target"), {"node", "path"});
            const auto path = channel.at("target").at("path");
            require(path == "translation" || path == "rotation" || path == "scale",
                    "Unsupported canonical animation target");
        }
        expected.insert(filename);
        outputs.push_back(filename);
    }
    JobDirectory job(project, "model-animation");
    expected.insert("owner.lock");
    for (const auto& input : inputs)
        write(job.path / input.name, input.bytes);
    // Reuse CPU container/accessor admission without linking native model codecs
    // into the parent. Every possible converter URI has already been restricted.
    GltfSourceLimits source_limits;
    source_limits.file_bytes = limit;
    source_limits.json_bytes = limit;
    source_limits.total_bytes = 2 * limit;
    source_limits.source_files = 2;
    const auto admitted = capture_gltf_source(job.path, "source.gltf", {}, source_limits, stop);
    (void)validate_gltf_accessors(admitted, {}, stop);
    WorkerLimits limits;
    limits.memory_bytes = 1024ull * 1024 * 1024;
    limits.file_bytes = limit;
    limits.total_bytes = 320ull * 1024 * 1024;
    limits.files = 69; // Up to68 converter input/output files plus the owned marker.
    limits.seconds = 240;
    limits.cpu_seconds = 220;
    run_worker(WorkerKind::Animation, executable, job.path, stop, limits, job.owner.get());
    for (const auto& file : std::filesystem::directory_iterator(job.path)) {
        ordinary(file.path());
        require(file.is_regular_file() && expected.erase(file.path().filename().string()) == 1,
                "Unexpected model converter output");
    }
    require(expected.empty(), "Missing model converter output");
    for (const auto& input : inputs)
        require(read_bytes(job.path / input.name, limit) == input.bytes,
                "Model converter changed its immutable input");
    std::vector<ArtifactFile> result;
    std::size_t budget = 256 * 1024 * 1024;
    for (const auto& name : outputs) {
        require(!stop.stop_requested(), "Model converter output collection cancelled");
        auto bytes = read_bytes(job.path / name, std::min(limit, budget));
        budget -= bytes.size();
        result.push_back({name, std::move(bytes)});
    }
    return result;
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
