#pragma once
#include <forge/assets.hpp>
#include <forge/scene.hpp>
namespace forge {
// UI-independent scene operations. Source creation does not silently convert or
// delete the original subtree; instantiate the new asset explicitly.
PrefabDocument create_prefab_source(const Scene& scene, const std::string& root);
std::string instantiate_prefab(Scene& scene, AssetId asset);
class PrefabLibrary {
  public:
    explicit PrefabLibrary(std::filesystem::path project);
    void refresh(Scene& scene);
    void load_scene(Scene& scene, const Json& document);
    const std::map<AssetId, AssetRecord>& records() const { return records_; }
    Json source(AssetId asset) const;
    AssetId create(Scene& scene, PrefabDocument document, const std::filesystem::path& relative);
    AssetId duplicate(Scene& scene, AssetId asset, const std::filesystem::path& relative);
    void publish(Scene& scene, const Json& expected, Json candidate);

  private:
    std::filesystem::path project_;
    AssetCatalog catalog_;
    std::map<AssetId, AssetRecord> records_;
    PrefabSources scan(AssetCatalog&, std::map<AssetId, AssetRecord>&) const;
    std::filesystem::path locate(const std::filesystem::path& relative) const;
};
} // namespace forge
