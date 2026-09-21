#include "source_copy.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include <algorithm>
#include <forge/gltf_source.hpp>
#include <forge/project_paths.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
namespace forge {
#ifdef FORGE_SOURCE_COPY_TESTING
void source_copy_before_rename();
#endif
namespace {
constexpr std::uint64_t file_limit = 512ull * 1024 * 1024;
constexpr std::uint64_t batch_limit = 2ull * 1024 * 1024 * 1024;
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void cancel(std::stop_token stop) {
    require(!stop.stop_requested(), "Source copy cancelled; no destination published");
}
std::string lower(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
void raw_path(const std::filesystem::path& relative) {
    (void)ProjectPaths::normalize(relative);
    for (const auto& part : relative) {
        const auto name = lower(path_utf8(part));
        require(
            !name.starts_with('.') && !name.ends_with(".forge-import.json") &&
                name != "forge.project.json" && name != "forge.assets.json" &&
                name != "forge.components.json" && !name.ends_with(".scene.json") &&
                !name.ends_with(".prefab.json") && !name.ends_with(".material.json") &&
                !name.ends_with(".shader.json"),
            "Import raw sources only; project/asset identities and hidden files are not copied");
    }
}
std::filesystem::path destination(const ProjectPaths& paths, const std::filesystem::path& folder) {
    require(ProjectPaths::normalize(folder) == folder && folder.begin()->string() == "Assets" &&
                std::distance(folder.begin(), folder.end()) >= 2,
            "Choose a new folder below Assets");
    raw_path(folder);
    const auto path = paths.resolve(folder);
    asset_storage::ordinary(path);
    require(!std::filesystem::exists(path) && std::filesystem::is_directory(path.parent_path()),
            "Import destination already exists or its parent folder is missing; choose a new name");
    return path;
}
void rename_new_directory(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::filesystem::filesystem_error(
            "Cannot publish imported source folder", from, to,
            std::error_code(GetLastError(), std::system_category()));
#else
    // rename() may replace an existing empty directory on POSIX. NOREPLACE is
    // necessary even after preflight: another process may create the destination.
    if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE))
        throw std::filesystem::filesystem_error("Cannot publish imported source folder", from, to,
                                                std::error_code(errno, std::generic_category()));
#endif
}
} // namespace
SourceCopyPlan prepare_source_copy(const std::filesystem::path& project,
                                   const std::filesystem::path& new_folder,
                                   const std::vector<std::filesystem::path>& files,
                                   const std::set<std::string>& extensions, std::stop_token stop) {
    const ProjectPaths paths(project);
    (void)destination(paths, new_folder);
    require(!files.empty() && files.size() <= 256, "Select between1 and256 source files");
    SourceCopyPlan result{paths.root(), new_folder, {}, {}, 0};
    std::set<std::filesystem::path, ProjectLocatorLess> selected;
    for (const auto& file : files) {
        cancel(stop);
        const auto path = std::filesystem::absolute(file).lexically_normal();
        asset_storage::ordinary(path);
        require(std::filesystem::is_regular_file(path), "Selected source must be an ordinary file");
        require(selected.insert(path).second, "The same source was selected more than once");
        raw_path(path.filename());
        const auto folder =
            std::filesystem::path("Source-" + std::to_string(result.sources.size() + 1));
        result.sources.push_back(folder / path.filename());
        std::map<std::filesystem::path, std::string, ProjectLocatorLess> dependencies;
        const auto extension = lower(path_utf8(path.extension()));
        if (extension == ".gltf" || extension == ".glb") {
            const auto bundle =
                capture_gltf_source(path.parent_path(), path.filename(), extensions, {}, stop);
            dependencies.emplace(path.filename(), bundle.source_digest);
            for (const auto& dependency : bundle.dependencies)
                dependencies.emplace(dependency.source, dependency.revision);
        } else {
            const auto bytes = asset_detail::read_bytes(path, file_limit);
            dependencies.emplace(path.filename(), asset_detail::content_digest(bytes));
        }
        for (const auto& [relative, expected] : dependencies) {
            cancel(stop);
            raw_path(relative);
            const auto original = ProjectPaths(path.parent_path()).resolve(relative);
            asset_storage::ordinary(original);
            const auto size = std::filesystem::file_size(original);
            require(size <= file_limit && size <= batch_limit - result.bytes &&
                        result.files.size() < 8192,
                    "Source copy exceeds512MiB per file,2GiB total or8192 files");
            result.bytes += size;
            result.files.push_back({original, folder / relative, expected, size});
        }
    }
    return result;
}
void commit_source_copy(const ProjectLease& lease, const SourceCopyPlan& plan,
                        std::stop_token stop) {
    lease.check();
    const ProjectPaths paths(lease.root());
    require(paths.root() == plan.project, "Source-copy plan belongs to another project");
    const auto target = destination(paths, plan.destination);
    require(!plan.files.empty() && plan.files.size() <= 8192 && plan.bytes <= batch_limit,
            "Invalid source-copy plan bounds");
    cancel(stop);
    const auto staging = target.parent_path() / (".forge-import-" + AssetId::generate().str());
    require(std::filesystem::create_directory(staging),
            "Cannot create isolated source-copy staging");
    std::map<std::filesystem::path, std::string> written;
    std::set<std::filesystem::path> directories{staging};
    bool published = false;
    try {
        const ProjectPaths staged(staging);
        std::set<std::filesystem::path, ProjectLocatorLess> destinations;
        std::uint64_t total = 0;
        for (const auto& entry : plan.files) {
            cancel(stop);
            lease.check();
            raw_path(entry.destination);
            require(destinations.insert(entry.destination).second && entry.bytes <= file_limit &&
                        entry.bytes <= batch_limit - total,
                    "Duplicate or excessive source-copy destination");
            total += entry.bytes;
            asset_storage::ordinary(entry.original);
            const auto bytes = asset_detail::read_bytes(entry.original, file_limit);
            require(bytes.size() == entry.bytes &&
                        asset_detail::content_digest(bytes) == entry.digest,
                    "A source changed after review; prepare the import again");
            const auto path = staged.resolve(entry.destination);
            for (auto parent = path.parent_path(); parent != staging; parent = parent.parent_path())
                directories.insert(parent);
            std::filesystem::create_directories(path.parent_path());
            asset_storage::replace(
                path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            written.emplace(path, entry.digest);
        }
        require(total == plan.bytes, "Source-copy plan byte count mismatch");
        cancel(stop);
        for (auto it = directories.rbegin(); it != directories.rend(); ++it)
            asset_storage::sync_directory(*it);
        lease.check();
        (void)destination(paths, plan.destination);
#ifdef FORGE_SOURCE_COPY_TESTING
        source_copy_before_rename();
#endif
        rename_new_directory(staging, target);
        published = true;
        asset_storage::sync_directory(target.parent_path());
    } catch (const std::exception& error) {
        if (published)
            throw std::runtime_error(
                std::string("Source folder was published but final flush failed: ") + error.what());
        // Retire only bytes this operation wrote. Unknown/externally changed
        // staging files remain for inspection instead of recursive deletion.
        std::error_code ignored;
        for (const auto& [path, digest] : written) {
            try {
                const auto bytes = asset_detail::read_bytes(path, file_limit);
                if (asset_detail::content_digest(bytes) == digest)
                    std::filesystem::remove(path, ignored);
            } catch (const std::exception&) {
            }
        }
        for (auto it = directories.rbegin(); it != directories.rend(); ++it)
            std::filesystem::remove(*it, ignored);
        throw std::runtime_error(
            std::string(error.what()) +
            (std::filesystem::exists(staging) ? "; preserved staging: " + path_utf8(staging) : ""));
    }
}
} // namespace forge
