#include "worker_staging.hpp"
#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "native_io_path.hpp"
#include "worker_stage_lease.hpp"
#include <algorithm>
#include <forge/identity.hpp>
#include <forge/project_paths.hpp>
#include <set>

namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
constexpr std::uint64_t maximum_bytes = 16ull * 1024 * 1024 * 1024;
struct Layout {
    std::string_view kind, parent, format;
};
constexpr Layout layouts[]{
    {"import", ".forge/jobs", "forge.import-job"},
    {"model-animation", ".forge/jobs", "forge.import-job"},
    {"animation", ".forge/animation-staging", "forge.worker-stage"},
    {"navigation", ".forge/navigation-staging", "forge.worker-stage"},
    {"script", ".forge/cache/script", "forge.worker-stage"},
    {"schema", ".forge/schema-jobs", "forge.worker-stage"},
    {"ui-inspection", ".forge/ui-inspection", "forge.worker-stage"},
    {"audio-stream", ".forge/audio-streams", "forge.worker-stage"},
};
const Layout& layout(std::string_view kind) {
    for (const auto& value : layouts)
        if (value.kind == kind)
            return value;
    throw std::runtime_error("Unknown worker staging layout");
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void ordinary(const std::filesystem::path& path) {
    require(native_io_path(std::filesystem::weakly_canonical(path)) == native_io_path(path) &&
                !std::filesystem::is_symlink(path),
            "Worker staging redirects; retained for inspection");
}
bool portable_file(std::string_view name) {
    if (name.empty() || name.size() > 128 || name.front() == '.' || name.back() == '.' ||
        !std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                   c == '-';
        }))
        return false;
    const auto stem = name.substr(0, name.find('.'));
    return stem != "con" && stem != "nul" && stem != "aux" && stem != "prn" &&
           !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem[3] >= '1' && stem[3] <= '9');
}
struct File {
    std::filesystem::path path;
    std::uint64_t bytes;
    std::filesystem::file_time_type modified;
};
struct Budget {
    std::size_t files = 0;
    std::uint64_t bytes = 0, metadata = 0;
};
void file(const std::filesystem::directory_entry& entry, std::vector<File>& files, Budget& budget) {
    ordinary(entry.path());
    require(entry.is_regular_file() && std::filesystem::hard_link_count(entry.path()) == 1,
            "Nonregular or aliased worker file retained for inspection");
    const auto bytes = entry.file_size();
    require(++budget.files <= 100000 && bytes <= maximum_bytes - budget.bytes,
            "Worker cleanup file/byte budget exceeded; remaining contents retained");
    budget.bytes += bytes;
    files.push_back({entry.path(), bytes, entry.last_write_time()});
}
void flat(const std::filesystem::path& directory, const std::set<std::string>* names,
          std::vector<File>& files, Budget& budget, std::stop_token stop) {
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        require(!stop.stop_requested(), "Worker cleanup cancelled");
        const auto name = path_utf8(entry.path().filename());
        require(portable_file(name) && (!names || names->contains(name)),
                "Unrecognized worker file retained for inspection");
        file(entry, files, budget);
    }
}
Json cleanup_job(const std::filesystem::path& job, Budget& budget, std::stop_token stop) {
    const auto name = path_utf8(job.filename());
    Json row{{"job", name}, {"removed", false}};
    try {
        require(!stop.stop_requested(), "Worker cleanup cancelled");
        require(AssetId::parse(name).str() == name, "Unrecognized worker directory name");
        ordinary(job);
        require(std::filesystem::is_directory(job), "Worker entry is not a directory");
        const auto marker_path = job / "owner.lock";
        auto owner = WorkerStageLease::try_open(marker_path);
        require(bool(owner), "Worker ownership is still active; retry after it exits");
        const auto text = owner->marker();
        const auto marker = parse_bounded_json(std::as_bytes(std::span(text)), 512, 64, 4);
        require(marker.is_object() && marker.size() == 4 && marker.at("version") == 1 &&
                    marker.at("job").get<std::string>() == name,
                "Unknown worker marker format or identity; retained for inspection");
        const auto kind = marker.at("kind").get<std::string>();
        const auto& shape = layout(kind);
        require(marker.at("format") == Json(shape.format) &&
                    job.parent_path().filename() == std::filesystem::path(shape.parent).filename(),
                "Worker marker does not match staging layout");
        std::set<std::string> input_names;
        const auto request = job / "request.json";
        if (kind == "import" && std::filesystem::exists(request)) {
            ordinary(request);
            require(std::filesystem::is_regular_file(request) &&
                        std::filesystem::hard_link_count(request) == 1 &&
                        budget.metadata < 64 * 1024 * 1024,
                    "Aliased request or exhausted cleanup metadata budget; retained");
            const auto bytes = read_bytes(request, 4 * 1024 * 1024);
            require(bytes.size() <= 64 * 1024 * 1024 - budget.metadata,
                    "Worker cleanup metadata budget exceeded");
            budget.metadata += bytes.size();
            const auto value = parse_bounded_json(bytes, 4 * 1024 * 1024, 400000, 32);
            require(value.at("format") == "forge.import-worker" && value.at("version") == 1 &&
                        value.at("inputs").is_array() && value.at("inputs").size() <= 65536,
                    "Unknown worker input manifest; retained for inspection");
            for (const auto& input : value.at("inputs")) {
                const auto input_name = input.at("name").get<std::string>();
                require(portable_file(input_name) && input_names.insert(input_name).second,
                        "Invalid or duplicate worker input name");
            }
        }
        std::set<std::string> root_files;
        if (kind == "model-animation" || kind == "animation") {
            root_files = {"source.gltf", "config.json", "skeleton.ozz"};
            const unsigned clips = kind == "model-animation" ? 64 : 8;
            for (unsigned i = 0; i < clips; ++i)
                root_files.insert("clip-" + std::to_string(i) + ".ozz");
            if (kind == "model-animation")
                root_files.insert("animation.bin");
            else
                for (unsigned i = 0; i < 16; ++i)
                    root_files.insert("buffer-" + std::to_string(i) + ".bin");
        } else if (kind == "audio-stream") {
            root_files = {"audio.wav"};
        } else if (kind == "navigation") {
            root_files = {"source.json", "navigation.fnav", "error.txt"};
        } else {
            root_files = {"request.json", "result.json", "error.json"};
            if (kind == "import") {
                root_files.erase("result.json");
                root_files.insert("status.json");
            }
            if (kind == "script")
                root_files.erase("error.json");
        }
        std::vector<File> files;
        std::vector<std::filesystem::path> directories;
        for (const auto& entry : std::filesystem::directory_iterator(job)) {
            require(!stop.stop_requested(), "Worker cleanup cancelled");
            const auto item = path_utf8(entry.path().filename());
            if (item == "owner.lock")
                continue;
            ordinary(entry.path());
            if (kind == "import" &&
                (item == "input" || item == "output" || item == "cancel.request")) {
                require(entry.is_directory(), "Worker control directory changed type");
                if (item == "cancel.request")
                    require(std::filesystem::is_empty(entry.path()),
                            "Unknown cancellation contents retained");
                else
                    flat(entry.path(), item == "input" ? &input_names : nullptr, files, budget,
                         stop);
                directories.push_back(entry.path());
            } else {
                require(root_files.contains(item),
                        "Unknown worker root contents retained for inspection");
                file(entry, files, budget);
            }
        }
        // Preflight the whole job before deleting any part of it. No live
        // cooperating process can regain this unique, unpublished stage.
        for (const auto& value : files) {
            ordinary(value.path);
            require(std::filesystem::is_regular_file(value.path) &&
                        std::filesystem::hard_link_count(value.path) == 1 &&
                        std::filesystem::file_size(value.path) == value.bytes &&
                        std::filesystem::last_write_time(value.path) == value.modified,
                    "Worker contents changed during cleanup; retained");
        }
        require(!stop.stop_requested(), "Worker cleanup cancelled before removal");
        // Keep input-name evidence until every payload is gone, so a crash
        // during cleanup leaves a stage that the next pass can still admit.
        std::stable_sort(files.begin(), files.end(), [&](const auto& a, const auto& b) {
            return (a.path == request) < (b.path == request);
        });
        for (const auto& value : files)
            require(std::filesystem::remove(value.path), "Worker file disappeared during cleanup");
        for (const auto& directory : directories)
            require(std::filesystem::remove(directory), "Worker directory has new contents");
        // Windows sharing exclusion protects the marker through preflight
        // and deletion. Release only for its final removal, then remove the
        // empty directory without recursively erasing any new arrival.
        owner.reset();
        require(std::filesystem::remove(marker_path) && std::filesystem::remove(job),
                "Worker directory changed during final cleanup");
        row["removed"] = true;
    } catch (const std::exception& error) {
        row["diagnostic"] = std::string(error.what()).substr(0, 8192);
    }
    return row;
}
} // namespace
WorkerStaging::WorkerStaging(const std::filesystem::path& project, std::string_view kind) {
    const auto& shape = layout(kind);
    // Keep the lexical private location until the redirect check. Resolving
    // first would hide an in-project symlink to authored storage.
    const auto parent = ProjectPaths(project).root() / shape.parent;
    ordinary(parent);
    std::filesystem::create_directories(native_io_path(parent));
    ordinary(parent);
    path = parent / AssetId::generate().str();
    require(std::filesystem::create_directory(native_io_path(path)),
            "Cannot reserve unique worker staging");
    // A failed marker write leaves an unrecognized private folder for inspection.
    owner = std::make_unique<WorkerStageLease>(path / "owner.lock",
                                               Json{{"format", shape.format},
                                                    {"version", 1},
                                                    {"job", path.filename().string()},
                                                    {"kind", kind}}
                                                   .dump());
}
WorkerStaging::~WorkerStaging() {
    owner.reset();
    try {
        (void)cleanup_worker_stage(path);
    } catch (...) {
        // Destruction must preserve the producer's result/diagnostic. Retain
        // active or unfamiliar contents for writer-owned maintenance.
    }
}
Json cleanup_worker_stage(const std::filesystem::path& job, std::stop_token stop) {
    Budget budget;
    return cleanup_job(native_io_path(job), budget, stop);
}
Json cleanup_worker_stages(const ProjectLease& lease, std::stop_token stop) {
    lease.check();
    const ProjectPaths paths(lease.root());
    std::set<std::string_view> parents;
    for (const auto& shape : layouts)
        parents.insert(shape.parent);
    std::vector<std::filesystem::path> jobs;
    for (const auto parent : parents) {
        require(!stop.stop_requested(), "Worker cleanup cancelled");
        const auto root = native_io_path(paths.root() / parent);
        ordinary(root);
        if (!std::filesystem::exists(root))
            continue;
        require(std::filesystem::is_directory(root), "Worker staging root is not a directory");
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            require(!stop.stop_requested(), "Worker cleanup cancelled");
            require(jobs.size() < 4096, "Too many worker entries; retained for inspection");
            jobs.push_back(entry.path());
        }
    }
    std::sort(jobs.begin(), jobs.end());
    Json result = Json::array();
    Budget budget;
    for (const auto& job : jobs)
        result.push_back(cleanup_job(job, budget, stop));
    return result;
}
} // namespace forge::asset_detail
