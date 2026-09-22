#include "asset_bytes.hpp"
#include <algorithm>
#include <chrono>
#include <forge/derived_cache.hpp>
#include <forge/project_paths.hpp>
#include <fstream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t manifest_limit = 4 * 1024 * 1024;
void ordinary_path(const std::filesystem::path& path) {
    if (std::filesystem::weakly_canonical(path) != path || std::filesystem::is_symlink(path))
        throw std::runtime_error("Cache path must not redirect: " + path_utf8(path));
}
// A separate local-filesystem lock allows the editor and headless cache readers to
// cooperate without claiming authored-project writer ownership. OS release handles crashes.
class CacheLock {
  public:
    explicit CacheLock(const std::filesystem::path& root) {
        ordinary_path(root);
        const auto path = root / "cache.lock";
        ordinary_path(path);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
#ifdef _WIN32
            handle_ =
                CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(handle_, &info) ||
                    (info.dwFileAttributes &
                     (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
                    CloseHandle(handle_);
                    handle_ = INVALID_HANDLE_VALUE;
                    throw std::runtime_error("Invalid cache lock file");
                }
                break;
            }
            if (GetLastError() != ERROR_SHARING_VIOLATION)
                throw std::runtime_error("Cannot open cache lock (Windows error " +
                                         std::to_string(GetLastError()) + ")");
#else
            handle_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (handle_ < 0)
                throw std::runtime_error("Cannot open cache lock");
            if (flock(handle_, LOCK_EX | LOCK_NB) == 0) {
                struct stat held{}, current{};
                if (fstat(handle_, &held) || lstat(path.c_str(), &current) ||
                    !S_ISREG(held.st_mode) || held.st_dev != current.st_dev ||
                    held.st_ino != current.st_ino) {
                    close(handle_);
                    handle_ = -1;
                    throw std::runtime_error("Cache lock was replaced");
                }
                break;
            }
            const auto error = errno;
            close(handle_);
            handle_ = -1;
            if (error != EWOULDBLOCK && error != EAGAIN)
                throw std::runtime_error("Cannot lock cache");
#endif
            if (std::chrono::steady_clock::now() >= end)
                throw std::runtime_error("Cache is busy; retry the operation");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    CacheLock(const CacheLock&) = delete;
    CacheLock& operator=(const CacheLock&) = delete;
    ~CacheLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
#else
        if (handle_ >= 0)
            close(handle_);
#endif
    }

  private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
};
void durable_file(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.flush();
    }
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot flush cache file");
    const bool ok = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
#else
    const auto fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        throw std::runtime_error("Cannot flush cache file");
    const bool ok = fsync(fd) == 0;
    close(fd);
#endif
    if (!ok)
        throw std::runtime_error("Cannot durably flush cache output: " + path_utf8(path));
}
void sync_directory(const std::filesystem::path& path) {
#ifndef _WIN32
    const auto fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        throw std::runtime_error("Cannot flush cache directory");
    const bool ok = fsync(fd) == 0;
    close(fd);
    if (!ok)
        throw std::runtime_error("Cannot durably flush cache directory");
#else
    (void)path; // Files are flushed; promotion uses MOVEFILE_WRITE_THROUGH below.
#endif
}
std::string file_name(const std::string& name) {
    // Flat immutable artifact files prevent symlink/subdirectory output traversal.
    if (name.empty() || name.size() > 128 || name == "manifest.json" ||
        !std::all_of(name.begin(), name.end(),
                     [](unsigned char c) {
                         return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                                c == '_' || c == '.';
                     }) ||
        ProjectPaths::normalize(name).filename() != name)
        throw std::runtime_error("Invalid artifact output name: " + name);
    return name;
}
Json parse_manifest(const std::vector<std::byte>& bytes) {
    return Json::parse(reinterpret_cast<const char*>(bytes.data()),
                       reinterpret_cast<const char*>(bytes.data() + bytes.size()),
                       [](int depth, Json::parse_event_t, Json&) {
                           if (depth > 64)
                               throw std::runtime_error("Cache manifest nesting exceeds limit");
                           return true;
                       });
}
void validator_required(const DerivedDataCache::Validator& validate) {
    if (!validate)
        throw std::runtime_error("Artifact format validator is required");
}
} // namespace

std::size_t CachedArtifact::byte_size() const {
    std::size_t total = 0;
    for (const auto& file : files)
        total += file.bytes.size();
    return total;
}
DerivedDataCache::DerivedDataCache(std::filesystem::path project, CacheLimits limits)
    : project_(std::filesystem::canonical(project)), limits_(limits) {
    if (!limits.files || limits.files > 4096 || !limits.file_bytes ||
        limits.file_bytes > limits.total_bytes ||
        limits.total_bytes > std::uint64_t(2) * 1024 * 1024 * 1024)
        throw std::runtime_error("Invalid cache resource limits");
    const ProjectPaths paths(project_);
    root_ = paths.resolve(".forge/cache/derived");
    if (root_ != project_ / ".forge/cache/derived")
        throw std::runtime_error("Derived cache must not redirect");
    std::filesystem::create_directories(root_);
    ordinary_path(root_);
}
CachedArtifact DerivedDataCache::read(const std::string& key) const {
    if (!valid_content_digest(key))
        throw std::runtime_error("Invalid cache key");
    const auto directory = root_ / key;
    ordinary_path(directory);
    const auto manifest_path = directory / "manifest.json";
    ordinary_path(manifest_path);
    auto manifest = parse_manifest(asset_detail::read_bytes(manifest_path, manifest_limit));
    if (manifest.at("version") != 1 || manifest.at("key") != key ||
        asset_build_digest(manifest.at("inputs")) != key || !manifest.at("files").is_array() ||
        manifest.at("files").empty() || manifest.at("files").size() > limits_.files)
        throw std::runtime_error("Invalid artifact manifest identity/format");
    CachedArtifact artifact{key, manifest, {}};
    std::size_t total = 0;
    std::set<std::string> names;
    for (const auto& file : manifest.at("files")) {
        const auto name = file_name(file.at("name"));
        if (!names.insert(name).second)
            throw std::runtime_error("Duplicate artifact filename");
        const auto& declared = file.at("bytes");
        if (!declared.is_number_unsigned())
            throw std::runtime_error("Invalid artifact byte count");
        const auto size = declared.get<std::uint64_t>();
        if (size > limits_.file_bytes || size > limits_.total_bytes - total)
            throw std::runtime_error("Artifact exceeds byte limit");
        const auto path = directory / name;
        ordinary_path(path);
        auto bytes = asset_detail::read_bytes(path, static_cast<std::size_t>(size));
        if (bytes.size() != size || file.at("sha256") != asset_detail::content_digest(bytes))
            throw std::runtime_error("Artifact size/hash mismatch: " + name);
        total += bytes.size();
        artifact.files.push_back({name, std::move(bytes)});
    }
    // Unmanifested files are not silently trusted or included in cooked packages.
    for (const auto& item : std::filesystem::directory_iterator(directory))
        if (item.path().filename() != "manifest.json" &&
            !names.contains(path_utf8(item.path().filename())))
            throw std::runtime_error("Unmanifested cache output");
    return artifact;
}
void DerivedDataCache::quarantine(const std::string& key, std::string_view reason) {
    const auto destination = root_ / ("quarantine-" + key + "-" + AssetId::generate().str());
    std::filesystem::rename(root_ / key, destination);
    const std::string message(reason.substr(0, 8192));
    // A corrupt entry may itself be a symlink. Never write through the moved entry.
    auto diagnostic = destination;
    diagnostic += ".txt";
    durable_file(diagnostic, std::as_bytes(std::span(message)));
}
CachedArtifact DerivedDataCache::load_selected(std::string_view key, const Validator& validate) {
    validator_required(validate);
    if (!valid_content_digest(key))
        throw std::runtime_error("Invalid selected artifact revision");
    // Selected revisions are immutable. Readers verify owned bytes and need no
    // writer lock file (runtime packages may be installed read-only). Concurrent
    // eviction can fail this request; it cannot publish partial/unvalidated data.
    auto artifact = read(std::string(key));
    validate(artifact);
    return artifact;
}
std::optional<CachedArtifact> DerivedDataCache::find(const AssetBuildInput& input,
                                                     const Validator& validate) {
    validator_required(validate);
    const auto key = input.key();
    CacheLock lock(root_);
    if (!std::filesystem::exists(root_ / key))
        return std::nullopt;
    std::optional<CachedArtifact> found;
    try {
        found = read(key);
        validate(*found);
    } catch (const std::exception& e) {
        quarantine(key, e.what());
        return std::nullopt;
    }
    // A bookkeeping write failure does not make validated content corrupt.
    std::error_code ignored;
    std::filesystem::last_write_time(root_ / key, std::filesystem::file_time_type::clock::now(),
                                     ignored);
    return found;
}
CachedArtifact DerivedDataCache::publish(const AssetBuildInput& input,
                                         std::vector<ArtifactFile> files,
                                         const Validator& validate) {
    validator_required(validate);
    if (files.empty() || files.size() > limits_.files)
        throw std::runtime_error("Invalid artifact file count");
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    auto entries = Json::array();
    std::size_t total = 0;
    std::string previous;
    for (const auto& file : files) {
        const auto name = file_name(file.name);
        if (name == previous || file.bytes.size() > limits_.file_bytes ||
            file.bytes.size() > limits_.total_bytes - total)
            throw std::runtime_error("Duplicate output or artifact byte limit exceeded");
        total += file.bytes.size();
        previous = name;
        entries.push_back({{"name", name},
                           {"bytes", file.bytes.size()},
                           {"sha256", asset_detail::content_digest(file.bytes)}});
    }
    const auto key = input.key();
    CachedArtifact candidate{
        key,
        {{"version", 1}, {"key", key}, {"inputs", input.document()}, {"files", std::move(entries)}},
        std::move(files)};
    validate(candidate);
    const auto manifest = candidate.manifest.dump();
    if (manifest.size() > manifest_limit)
        throw std::runtime_error("Artifact manifest exceeds limit");
    CacheLock lock(root_);
    const auto final = root_ / key;
    if (std::filesystem::exists(final)) {
        std::optional<CachedArtifact> existing;
        try {
            existing = read(key);
            validate(*existing);
        } catch (const std::exception& e) {
            quarantine(key, e.what());
            existing.reset();
        }
        if (existing) {
            if (existing->manifest != candidate.manifest)
                throw std::runtime_error("Nondeterministic artifact for identical build inputs; "
                                         "previous entry retained");
            return std::move(*existing);
        }
    }
    const auto stage = root_ / ("pending-" + AssetId::generate().str());
    std::filesystem::create_directory(stage);
    try {
        for (const auto& file : candidate.files)
            durable_file(stage / file.name, file.bytes);
        durable_file(stage / "manifest.json", std::as_bytes(std::span(manifest)));
        sync_directory(stage);
#ifdef _WIN32
        if (!MoveFileExW(stage.c_str(), final.c_str(), MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Artifact promotion failed (Windows error " +
                                     std::to_string(GetLastError()) + ")");
#else
        std::filesystem::rename(stage, final);
        sync_directory(root_);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove_all(stage, ignored);
        throw;
    }
    return candidate;
}
CacheStatistics DerivedDataCache::statistics() const {
    CacheLock lock(root_);
    CacheStatistics result;
    for (const auto& item : std::filesystem::directory_iterator(root_)) {
        const auto name = path_utf8(item.path().filename());
        if (name.starts_with("quarantine-") && !name.ends_with(".txt")) {
            ++result.quarantined;
            continue;
        }
        if (!valid_content_digest(name))
            continue;
        ordinary_path(item.path());
        ++result.entries;
        for (const auto& file : std::filesystem::directory_iterator(item.path())) {
            ordinary_path(file.path());
            if (file.is_regular_file())
                result.bytes += file.file_size();
        }
    }
    return result;
}
std::uint64_t DerivedDataCache::prune(std::uint64_t budget,
                                      const std::set<std::string>& protected_keys) {
    for (const auto& key : protected_keys)
        if (!valid_content_digest(key))
            throw std::runtime_error("Invalid protected artifact key");
    CacheLock lock(root_);
    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type access;
        std::uint64_t bytes;
    };
    std::vector<Entry> entries;
    std::uint64_t total = 0, removed = 0;
    for (const auto& item : std::filesystem::directory_iterator(root_)) {
        const auto name = path_utf8(item.path().filename());
        if (!valid_content_digest(name))
            continue;
        ordinary_path(item.path());
        std::uint64_t bytes = 0;
        for (const auto& file : std::filesystem::directory_iterator(item.path())) {
            ordinary_path(file.path());
            if (file.is_regular_file())
                bytes += file.file_size();
        }
        total += bytes;
        if (!protected_keys.contains(name))
            entries.push_back({item.path(), item.last_write_time(), bytes});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.access < b.access || (a.access == b.access && a.path < b.path);
    });
    for (const auto& entry : entries) {
        if (total <= budget)
            break;
        std::filesystem::remove_all(entry.path);
        total -= entry.bytes;
        removed += entry.bytes;
    }
    return removed;
}
Json DerivedDataCache::verify(const Validator& validate) {
    validator_required(validate);
    CacheLock lock(root_);
    auto results = Json::array();
    std::vector<std::string> keys;
    for (const auto& item : std::filesystem::directory_iterator(root_)) {
        auto key = path_utf8(item.path().filename());
        if (valid_content_digest(key))
            keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        try {
            validate(read(key));
            results.push_back({{"key", key}, {"ok", true}});
        } catch (const std::exception& e) {
            results.push_back({{"key", key}, {"ok", false}, {"diagnostic", e.what()}});
            quarantine(key, e.what());
        }
    }
    return results;
}
} // namespace forge
