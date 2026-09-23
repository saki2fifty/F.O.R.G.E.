#pragma once
#include <filesystem>
#include <forge/asset_build.hpp>
#include <forge/asset_ref.hpp>
#include <map>
#include <optional>
#include <vector>
namespace forge {
inline constexpr std::size_t max_asset_index_bytes = 64 * 1024 * 1024;
inline bool is_declared_runtime_dependency(const AssetDependency& edge) {
    return edge.kind == AssetDependencyKind::Runtime && edge.role.starts_with("declared:");
}
struct AssetSubasset {
    AssetId owner;
    // Durable mapping-entry key, not the current source array index or display name.
    std::string key;
    bool removed = false;
    bool operator==(const AssetSubasset&) const = default;
};
struct AssetRecord {
    AssetId id;
    std::string type;
    std::filesystem::path source;
    unsigned schema_version = 1;
    std::vector<AssetId> dependencies;
    nlohmann::json metadata = nlohmann::json::object();
    // Empty for v1 records. New importers record typed edges; dependencies remains
    // their sorted target projection for existing subsystem callers.
    std::vector<AssetDependency> dependency_edges;
    std::vector<AssetSourceDependency> source_dependencies;
    std::optional<AssetSubasset> subasset;
};
enum class AssetState { Available, Missing, Unresolved, Incompatible, Removed };
struct AssetResolution {
    AssetState state = AssetState::Unresolved;
    std::optional<AssetRecord> record;
    std::string diagnostic;
};
// Project-scoped metadata service. No ECS world, GPU resources or resource loading.
class AssetCatalog {
  public:
    explicit AssetCatalog(std::filesystem::path project);
    void add(AssetRecord record);
    void replace(AssetRecord record);
    // Validate a complete candidate once, then swap records and both graph indexes.
    void replace_all(std::vector<AssetRecord> records);
    const std::map<AssetId, AssetRecord>& records() const { return records_; }
    const AssetDependencyGraph& dependency_graph() const { return graph_; }
    std::vector<AssetId> members(AssetId owner, bool include_removed = false) const;
    void set_dependencies(AssetId consumer, std::vector<AssetDependency> edges);
    void set_source_dependencies(AssetId consumer, std::vector<AssetSourceDependency> sources);
    static AssetCatalog open_project(const std::filesystem::path& root);
    static std::filesystem::path project_index(const std::filesystem::path& root);
    static AssetRecord register_audio_clip(const std::filesystem::path& project,
                                           const std::filesystem::path& source);
    AssetRecord add_scene(const std::filesystem::path& source);
    void relocate(AssetId id, const std::filesystem::path& source);
    AssetResolution resolve(AssetId id, const std::string& expected_type) const;
    template <class T> AssetResolution resolve(AssetRef<T> ref) const {
        return resolve(ref.id, T::type);
    }
    void save(const std::filesystem::path& index) const;
    void load(const std::filesystem::path& index);
    nlohmann::json document() const;
    void restore(const nlohmann::json& document);

  private:
    std::filesystem::path locate(const std::filesystem::path& source) const;
    std::filesystem::path project_;
    std::map<AssetId, AssetRecord> records_;
    AssetDependencyGraph graph_;
};
} // namespace forge
