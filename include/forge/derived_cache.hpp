#pragma once
#include <filesystem>
#include <forge/asset_build.hpp>
#include <optional>
#include <stop_token>

namespace forge {
struct ArtifactFile {
    std::string name;
    std::vector<std::byte> bytes;
};
struct CachedArtifact {
    std::string key;
    nlohmann::json manifest;
    std::vector<ArtifactFile> files;
    std::size_t byte_size() const;
};
struct CacheLimits {
    std::size_t file_bytes = 256 * 1024 * 1024;
    std::size_t total_bytes = 512 * 1024 * 1024;
    std::size_t files = 256;
};
struct CacheStatistics {
    std::uint64_t entries = 0, bytes = 0, quarantined = 0;
};
// The format admission callback is mandatory for hits as well as fresh outputs.
// Returned bytes are owned copies: clearing the disk cache cannot invalidate a resource.
class DerivedDataCache {
  public:
    using Validator = std::function<void(const CachedArtifact&)>;
    explicit DerivedDataCache(std::filesystem::path project, CacheLimits limits = {});
    std::optional<CachedArtifact> find(const AssetBuildInput& input, const Validator& validate);
    // Runtime/tool readers can load an already selected immutable revision without
    // the importer or original source. Missing/corrupt data throws; no publication.
    CachedArtifact load_selected(std::string_view key, const Validator& validate);
    CachedArtifact publish(const AssetBuildInput& input, std::vector<ArtifactFile> files,
                           const Validator& validate);
    CacheStatistics statistics() const;
    // Caller supplies selected revisions/active jobs. Eviction never guesses reachability.
    std::uint64_t prune(std::uint64_t budget_bytes, const std::set<std::string>& protected_keys);
    // Explicit tooling operation. Removes only the supplied immutable keys;
    // callers own project/job coordination. Loaded resources own their bytes.
    std::uint64_t erase(const std::set<std::string>& keys);
    // Publication stages are created while holding the cache lock. Acquiring it
    // proves that a retained stage is no longer owned by a cooperating publisher.
    // Unknown/non-flat/redirected paths are retained with per-entry diagnostics.
    nlohmann::json cleanup_orphans(bool include_quarantine = false);
    std::set<std::string> keys() const;
    // Maintenance-only hash/manifest audit. Does not claim format admission,
    // select resources, or quarantine data belonging to an unknown importer.
    nlohmann::json verify_storage(std::stop_token stop = {}) const;
    nlohmann::json verify(const Validator& validate);

  private:
    std::filesystem::path project_, root_;
    CacheLimits limits_;
    CachedArtifact read(const std::string& key) const;
    void quarantine(const std::string& key, std::string_view reason);
};
} // namespace forge
