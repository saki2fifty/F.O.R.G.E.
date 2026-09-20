#pragma once
#include <forge/asset_ref.hpp>
#include <forge/project_paths.hpp>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <vector>

namespace forge {
enum class AssetDependencyKind { Source, Build, Runtime, Optional, Subasset };
struct AssetDependency {
    AssetId target;
    std::string expected_type;
    AssetDependencyKind kind = AssetDependencyKind::Build;
    std::string role;
    std::string revision;
    auto operator<=>(const AssetDependency&) const = default;
};
// A source file is not a logical asset. Includes/buffers/images can affect an
// import without allocating persistent AssetIds merely to index filesystem bytes.
struct AssetSourceDependency {
    std::filesystem::path source;
    std::string role;
    std::string revision;
    auto operator<=>(const AssetSourceDependency&) const = default;
};
void to_json(nlohmann::json& value, const AssetDependency& edge);
void from_json(const nlohmann::json& value, AssetDependency& edge);
void to_json(nlohmann::json& value, const AssetSourceDependency& source);
void from_json(const nlohmann::json& value, AssetSourceDependency& source);
// Catalog-owned dependency index. A rejected replacement leaves both indexes unchanged.
class AssetDependencyGraph {
  public:
    void replace(AssetId consumer, std::vector<AssetDependency> edges);
    void replace_sources(AssetId consumer, std::vector<AssetSourceDependency> sources);
    const std::vector<AssetDependency>& dependencies(AssetId consumer) const;
    const std::vector<AssetSourceDependency>& source_dependencies(AssetId consumer) const;
    std::vector<AssetId> referrers(AssetId target) const;
    std::vector<AssetId> source_referrers(const std::filesystem::path& source) const;
    std::vector<AssetId> invalidated_by(AssetId target) const;
    std::vector<AssetId> invalidated_by_source(const std::filesystem::path& source) const;
    std::vector<AssetId> build_order(std::span<const AssetId> roots) const;
    nlohmann::json document() const;
    void restore(const nlohmann::json& document);

  private:
    std::size_t edge_count() const;
    std::map<AssetId, std::vector<AssetDependency>> forward_;
    std::map<AssetId, std::set<AssetId>> reverse_;
    std::map<AssetId, std::vector<AssetSourceDependency>> sources_;
    std::map<std::filesystem::path, std::set<AssetId>, ProjectLocatorLess> source_reverse_;
};

struct AssetBuildInput {
    std::string source_digest;
    std::string importer;
    std::string importer_revision;
    unsigned settings_version = 1;
    nlohmann::json settings = nlohmann::json::object();
    std::map<std::string, std::string> source_dependencies;
    std::vector<AssetDependency> dependencies;
    std::string output_format;
    unsigned output_version = 1;
    std::string platform;
    std::string backend;
    std::string profile;
    nlohmann::json document() const;
    std::string key() const;
};

// Canonical, bounded JSON identity. Rejects non-finite values rather than hashing null.
std::string asset_build_digest(const nlohmann::json& value);
bool valid_content_digest(std::string_view digest);
} // namespace forge
