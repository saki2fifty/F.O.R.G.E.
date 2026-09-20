#pragma once
#include <forge/identity.hpp>
namespace forge {
struct MeshAsset {
    static constexpr const char* type = "mesh";
};
struct MaterialAsset {
    static constexpr const char* type = "material";
};
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
} // namespace forge
