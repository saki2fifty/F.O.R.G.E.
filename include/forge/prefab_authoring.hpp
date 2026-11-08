#pragma once
#include <forge/assets.hpp>
#include <forge/scene.hpp>
namespace forge {
// UI-independent scene operations. Source creation does not silently convert or
// delete the original subtree; instantiate the new asset explicitly.
PrefabDocument create_prefab_source(const Scene& scene, const std::string& root);
std::string instantiate_prefab(Scene& scene, AssetId asset);
struct PrefabApplyCandidate {
    Json expected, source, scene;
    std::size_t overrides = 0;
    PrefabSources expected_bases;
};
class PrefabLibrary {
  public:
    static constexpr std::size_t max_source_bytes = 8 * 1024 * 1024;
    explicit PrefabLibrary(std::filesystem::path project);
    void refresh(Scene& scene);
    void load_scene(Scene& scene, const Json& document);
    const std::map<AssetId, AssetRecord>& records() const { return records_; }
    Json source(AssetId asset) const;
    // Review base/nested supporting sources against disk. A candidate reviews
    // the union needed before and after publication; retain it with the draft.
    PrefabSources base_sources(const Scene&, const Json& source,
                               const Json* candidate = nullptr) const;
    AssetId create(Scene& scene, PrefabDocument document, const std::filesystem::path& relative);
    AssetId create_variant(Scene&, AssetId base, const std::filesystem::path& relative);
    AssetId duplicate(Scene& scene, AssetId asset, const std::filesystem::path& relative);
    void publish(Scene& scene, const Json& expected, Json candidate,
                 const PrefabSources& expected_bases = {});
    PrefabApplyCandidate prepare_apply(const Scene&, const std::string& instance) const;
    std::filesystem::path source_path(AssetId) const;
    // Candidate publication reuses Scene's reconciliation and history authority.
    // Document owner supplies the coordinated durable pair and replay callback.
    void publish_apply(Scene&, const PrefabApplyCandidate&, const std::function<void()>&,
                       std::shared_ptr<Scene::PrefabReplay> replay = {});

  private:
    std::filesystem::path project_;
    AssetCatalog catalog_;
    std::map<AssetId, AssetRecord> records_;
    PrefabSources scan(AssetCatalog&, std::map<AssetId, AssetRecord>&) const;
    std::filesystem::path locate(const std::filesystem::path& relative) const;
};
} // namespace forge
