#pragma once
#include <filesystem>
#include <forge/asset_build.hpp>
#include <optional>

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
    CachedArtifact publish(const AssetBuildInput& input, std::vector<ArtifactFile> files,
                           const Validator& validate);
    CacheStatistics statistics() const;
    // Caller supplies selected revisions/active jobs. Eviction never guesses reachability.
    std::uint64_t prune(std::uint64_t budget_bytes, const std::set<std::string>& protected_keys);
    nlohmann::json verify(const Validator& validate);

  private:
    std::filesystem::path project_, root_;
    CacheLimits limits_;
    CachedArtifact read(const std::string& key) const;
    void quarantine(const std::string& key, std::string_view reason);
};
} // namespace forge
