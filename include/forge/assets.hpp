#pragma once
#include <filesystem>
#include <forge/identity.hpp>
#include <map>
#include <optional>
#include <vector>
namespace forge {
struct SceneAsset {
    static constexpr const char* type = "scene";
};
struct PrefabAsset {
    static constexpr const char* type = "prefab";
};
template <class T> struct AssetRef {
    AssetId id;
    auto operator<=>(const AssetRef&) const = default;
};
template <class T> void to_json(nlohmann::json& j, const AssetRef<T>& ref) { j = ref.id; }
template <class T> void from_json(const nlohmann::json& j, AssetRef<T>& ref) {
    ref.id = j.get<AssetId>();
}
struct AssetRecord {
    AssetId id;
    std::string type;
    std::filesystem::path source;
    unsigned schema_version = 1;
    std::vector<AssetId> dependencies;
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
