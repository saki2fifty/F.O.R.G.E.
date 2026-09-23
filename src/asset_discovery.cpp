#include "asset_bytes.hpp"
#include <algorithm>
#include <forge/asset_build.hpp>
#include <forge/asset_discovery.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace forge {
namespace {
std::string lowercase(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return value;
}
bool prefix(const std::filesystem::path& path, const std::filesystem::path& parent) {
    auto p = path.begin();
    for (const auto& part : parent) {
        if (p == path.end())
            return false;
#ifdef _WIN32
        if (CompareStringOrdinal(p->c_str(), -1, part.c_str(), -1, TRUE) != CSTR_EQUAL)
#else
        if (*p != part)
#endif
            return false;
        ++p;
    }
    return true;
}
bool filtered_locator(const std::filesystem::path& locator,
                      const std::vector<std::filesystem::path>& ignored) {
    for (const auto& part : locator) {
        const auto name = lowercase(path_utf8(part));
        if (name.starts_with('.') || name.starts_with('~') || name.ends_with('~') ||
            name.ends_with(".tmp") || name.ends_with(".pending") || name.ends_with(".swp") ||
            name.ends_with(".bak"))
            return true;
    }
    for (const auto& ignore : ignored)
        if (prefix(locator, ignore))
            return true;
    return false;
}
bool hidden_source(const std::filesystem::path& absolute) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(absolute.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Cannot read file attributes (OS error " +
                                 std::to_string(GetLastError()) + ")");
    return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
    (void)absolute;
    return false;
#endif
}
std::string source_kind(const std::filesystem::path& path) {
    const auto name = lowercase(path_utf8(path.filename()));
    // Recognition is deliberately separate from importer support/admission.
    for (const auto& [suffix, kind] : std::initializer_list<std::pair<const char*, const char*>>{
             {".scene.json", "scene"},
             {".prefab.json", "prefab"},
             {".material.json", "material"},
             {".collision.json", "collision"},
             {".shader.json", "shader_program"},
             {".gltf", "model"},
             {".glb", "model"},
             {".png", "image"},
             {".jpg", "image"},
             {".jpeg", "image"},
             {".tga", "image"},
             {".bmp", "image"},
             {".hdr", "image"},
             {".dds", "image"},
             {".ktx", "image"},
             {".ktx2", "image"},
             {".webp", "image"},
             {".exr", "image"},
             {".wav", "audio"},
             {".mp3", "audio"},
             {".flac", "audio"},
             {".ogg", "audio"},
             {".ttf", "font"},
             {".otf", "font"},
             {".rml", "ui_document"},
             {".rcss", "ui_style"},
             {".hlsl", "shader"},
             {".hlsli", "shader_include"},
             {".flecs", "script"},
             {".ozz", "animation_archive"}})
        if (name.ends_with(suffix))
            return kind;
    return "unrecognized";
}
bool same_contents(const SourceSnapshot& a, const SourceSnapshot& b) {
    if (a.files.size() != b.files.size())
        return false;
    for (const auto& [path, file] : a.files) {
        auto found = b.files.find(path);
        if (found == b.files.end() || file.digest != found->second.digest)
            return false;
    }
    return true;
}
std::vector<SourceChange> changes(const SourceSnapshot& before, const SourceSnapshot& after,
                                  std::uint64_t generation) {
    // Rename evidence requires BOTH a unique OS identity and unchanged content.
    // Copies/content duplicates and hardlink aliases never silently retarget an asset.
    std::map<std::string, std::vector<const SourceFile*>> old_ids, new_ids;
    for (const auto& [path, file] : before.files) {
        (void)path;
        if (!file.file_identity.empty())
            old_ids[file.file_identity].push_back(&file);
    }
    for (const auto& [path, file] : after.files) {
        (void)path;
        if (!file.file_identity.empty())
            new_ids[file.file_identity].push_back(&file);
    }
    std::set<std::filesystem::path> moved_from;
    std::vector<SourceChange> result;
    for (const auto& [path, file] : after.files) {
        auto old = before.files.find(path);
        if (old != before.files.end()) {
            if (old->second.digest != file.digest)
                result.push_back({SourceChangeKind::Modified, path, path, file.digest,
                                  old->second.digest, generation});
            continue;
        }
        auto previous = old_ids.find(file.file_identity);
        const auto current = new_ids.find(file.file_identity);
        if (previous != old_ids.end() && previous->second.size() == 1 && current != new_ids.end() &&
            current->second.size() == 1 && previous->second[0]->digest == file.digest &&
            !after.files.contains(previous->second[0]->source)) {
            const auto& original = *previous->second[0];
            moved_from.insert(original.source);
            result.push_back({SourceChangeKind::Moved, path, original.source, file.digest,
                              original.digest, generation});
        } else {
            result.push_back({SourceChangeKind::Created, path, {}, file.digest, {}, generation});
        }
    }
    for (const auto& [path, file] : before.files)
        if (!after.files.contains(path) && !moved_from.contains(path))
            result.push_back({SourceChangeKind::Removed, path, path, {}, file.digest, generation});
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.source < b.source; });
    return result;
}
} // namespace

SourceSnapshot scan_asset_sources(const std::filesystem::path& project,
                                  const SourceScanOptions& options, std::stop_token stop) {
    if ((options.roots.empty() && !options.include_project_root) || options.roots.size() > 256 ||
        options.ignored.size() > 1024 || !options.max_files || options.max_files > 1000000 ||
        !options.max_directories || options.max_directories > 100000 || !options.max_depth ||
        options.max_depth > 128 || !options.max_file_bytes ||
        options.max_file_bytes > 2ULL * 1024 * 1024 * 1024 || !options.max_total_bytes ||
        options.max_total_bytes > 1024ULL * 1024 * 1024 * 1024)
        throw std::runtime_error("Invalid asset scan limits");
    ProjectPaths paths(project);
    if (!std::filesystem::is_directory(paths.root()))
        throw std::runtime_error("Asset project root is not a directory");
    auto ignored = options.ignored;
    for (auto& path : ignored)
        path = ProjectPaths::normalize(path);
    struct Pending {
        std::filesystem::path path;
        std::size_t depth = 0;
    };
    std::vector<Pending> pending;
    for (const auto& root : options.roots)
        pending.push_back({ProjectPaths::normalize(root), 0});
    if (options.include_project_root) {
        // Explicit roots were still validated above. One traversal is sufficient
        // and avoids treating its ordinary descendants as directory aliases.
        pending.clear();
        pending.push_back({{}, 0});
    }
    std::sort(pending.begin(), pending.end(),
              [](const auto& a, const auto& b) { return a.path > b.path; });
    SourceSnapshot result;
    auto diagnostic = [&](const auto& path, std::string code, std::string message, bool error) {
        result.complete &= !error;
        if (result.diagnostics.size() < 256)
            result.diagnostics.push_back({path, std::move(code), std::move(message), error});
    };
    std::set<std::string> directories;
    std::map<std::string, std::filesystem::path> file_ids;
    std::size_t inspected = 0;
    while (!pending.empty()) {
        auto item = std::move(pending.back());
        pending.pop_back();
        if (stop.stop_requested()) {
            diagnostic(item.path, "cancelled", "Source scan cancelled", true);
            break;
        }
        if (++inspected > options.max_files + options.max_directories) {
            diagnostic(item.path, "entry_limit", "Source scan entry limit reached", true);
            break;
        }
        try {
            if (result.files.contains(item.path))
                continue;
            const bool project_root = item.path.empty();
            if (!project_root && filtered_locator(item.path, ignored)) {
                ++result.filtered;
                continue;
            }
            // Resolve contained symlinks/junctions before reading. As with other
            // project IO this is not a sandbox against concurrent host filesystem mutation.
            const auto absolute = project_root ? paths.root() : paths.resolve(item.path);
            if (!std::filesystem::exists(absolute)) {
                // A not-yet-created Assets root is an ordinary empty project.
                if (item.depth != 0)
                    diagnostic(item.path, "source_changed", "Source disappeared during scan", true);
                continue;
            }
            if (!project_root && hidden_source(absolute)) {
                ++result.filtered;
                continue;
            }
            if (std::filesystem::is_directory(absolute)) {
                if (!directories
                         .insert(project_root ? "<project-root>" : paths.file_identity(item.path))
                         .second) {
                    diagnostic(item.path, "directory_alias",
                               "Directory already scanned through another path", false);
                    continue;
                }
                if (directories.size() > options.max_directories ||
                    item.depth >= options.max_depth) {
                    diagnostic(item.path, "directory_limit",
                               "Source scan directory/depth limit reached", true);
                    continue;
                }
                std::vector<std::filesystem::path> children;
                for (const auto& child : std::filesystem::directory_iterator(absolute)) {
                    if (children.size() + pending.size() >=
                        options.max_files + options.max_directories)
                        throw std::runtime_error("Source directory entry limit reached");
                    children.push_back(item.path / child.path().filename());
                }
                std::sort(children.begin(), children.end(), std::greater<>{});
                for (auto& child : children)
                    pending.push_back({std::move(child), item.depth + 1});
                continue;
            }
            if (!std::filesystem::is_regular_file(absolute)) {
                diagnostic(item.path, "non_regular_source",
                           "Only regular source files are supported", true);
                continue;
            }
            if (result.files.size() >= options.max_files) {
                diagnostic(item.path, "file_limit", "Source scan file limit reached", true);
                break;
            }
            const auto length = std::filesystem::file_size(absolute);
            if (length > options.max_file_bytes ||
                length > options.max_total_bytes - result.bytes_read) {
                diagnostic(item.path, "byte_limit", "Source exceeds file/scan byte limit", true);
                continue;
            }
            const auto identity = paths.file_identity(item.path);
            const auto stamp = std::filesystem::last_write_time(absolute);
            const auto bytes = asset_detail::read_bytes(
                absolute,
                static_cast<std::size_t>(
                    std::min(options.max_file_bytes, options.max_total_bytes - result.bytes_read)));
            result.bytes_read += bytes.size();
            if (bytes.size() != length || paths.file_identity(item.path) != identity ||
                std::filesystem::last_write_time(absolute) != stamp ||
                paths.resolve(item.path) != absolute)
                throw std::runtime_error("Source changed during scan; retry");
            SourceFile file{item.path,
                            asset_detail::content_digest(bytes),
                            length,
                            identity,
                            source_kind(item.path),
                            {}};
            if (const auto previous = file_ids.find(identity); previous != file_ids.end()) {
                file.alias_of = previous->second;
                diagnostic(item.path, "source_alias",
                           "Source shares a file identity with " + path_utf8(file.alias_of), false);
            } else {
                file_ids.emplace(identity, item.path);
            }
            result.files.emplace(item.path, std::move(file));
        } catch (const std::exception& e) {
            diagnostic(item.path, "source_scan_failed", e.what(), true);
        }
    }
    return result;
}
SourceChangeTracker::SourceChangeTracker(SourceSnapshot baseline,
                                         std::chrono::milliseconds debounce)
    : baseline_(std::move(baseline)), latest_(baseline_), debounce_(debounce),
      complete_(baseline_.complete) {
    if (!baseline_.complete || debounce.count() < 0 || debounce > std::chrono::seconds(30))
        throw std::runtime_error(
            "Source tracker requires a complete baseline and bounded debounce");
}
void SourceChangeTracker::observe(SourceSnapshot snapshot, Clock::time_point now) {
    if (complete_ != snapshot.complete || !same_contents(snapshot, latest_)) {
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Source observation generation exhausted");
        ++generation_;
        changed_ = now;
        pending_ = true;
    }
    complete_ = snapshot.complete;
    latest_ = std::move(snapshot);
    if (complete_ && !pending_)
        baseline_ = latest_; // Track atomic same-content replacements for later move evidence.
}
std::vector<SourceChange> SourceChangeTracker::drain(Clock::time_point now) {
    if (!complete_ || !pending_ || now < changed_ || now - changed_ < debounce_)
        return {};
    auto result = changes(baseline_, latest_, generation_);
    std::erase_if(result, [&](const auto& change) {
        const auto found = writes_.find(change.source);
        if (found == writes_.end())
            return false;
        const bool own =
            found->second == change.digest &&
            (change.kind == SourceChangeKind::Created || change.kind == SourceChangeKind::Modified);
        writes_.erase(found);
        return own;
    });
    // A scan already in flight may report another file before observing our
    // write. Retain that acknowledgement until this path is actually observed.
    std::erase_if(writes_, [&](const auto& write) {
        const auto found = latest_.files.find(write.first);
        return found != latest_.files.end() && found->second.digest == write.second;
    });
    baseline_ = latest_;
    pending_ = false;
    return result;
}
void SourceChangeTracker::acknowledge_write(const std::filesystem::path& source,
                                            std::string digest) {
    if (!valid_content_digest(digest))
        throw std::runtime_error("Self-write acknowledgement needs a source digest");
    const auto locator = ProjectPaths::normalize(source);
    if (!writes_.contains(locator) && writes_.size() >= 4096)
        throw std::runtime_error("Too many pending source write acknowledgements");
    writes_[locator] = std::move(digest);
}
} // namespace forge
