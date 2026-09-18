#pragma once
#include <filesystem>
#include <forge/asset_ref.hpp>
#include <map>
#include <optional>
#include <vector>
namespace forge {
struct AssetRecord {
    AssetId id;
    std::string type;
    std::filesystem::path source;
    unsigned schema_version = 1;
    std::vector<AssetId> dependencies;
    nlohmann::json metadata = nlohmann::json::object();
};
enum class AssetState { Available, Missing, Unresolved, Incompatible };
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
    const std::map<AssetId, AssetRecord>& records() const { return records_; }
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

  private:
    std::filesystem::path locate(const std::filesystem::path& source) const;
    std::filesystem::path project_;
    std::map<AssetId, AssetRecord> records_;
};
} // namespace forge
