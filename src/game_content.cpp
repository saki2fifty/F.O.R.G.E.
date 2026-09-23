#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include <forge/game_content.hpp>
#include <forge/prefab.hpp>
#include <forge/scene.hpp>
#include <set>
namespace forge {
Json load_game_scene(const std::filesystem::path& root, AssetRef<SceneAsset> ref) {
    const ProjectPaths paths(root);
    const auto catalog = AssetCatalog::open_project(root);
    std::size_t bytes = 0;
    auto read = [&](AssetId id, std::string_view type) {
        const auto found = catalog.records().find(id);
        if (found == catalog.records().end() || found->second.type != type ||
            found->second.subasset)
            throw std::runtime_error("game.content: Missing or incompatible " + std::string(type) +
                                     " " + id.str());
        const auto version = found->second.schema_version;
        if (type == SceneAsset::type ? version < 3 || version > 5 : version < 1 || version > 2)
            throw std::runtime_error("game.content: Unsupported catalog schema version");
        auto data = asset_detail::read_bytes(paths.resolve(found->second.source), 64 * 1024 * 1024);
        if (data.size() > 256 * 1024 * 1024 - bytes)
            throw std::runtime_error("game.content: Scene/prefab closure exceeds 256 MiB");
        bytes += data.size();
        auto doc = asset_detail::parse_bounded_json(data, 64 * 1024 * 1024);
        if (doc.at("asset_id").get<AssetId>() != id)
            throw std::runtime_error("game.content: Scene/prefab identity differs from catalog");
        return doc;
    };
    auto scene = read(ref.id, SceneAsset::type);
    Scene::validate_document(scene);
    if (scene.at("version") < 3 || scene.contains("_prefab_sources"))
        throw std::runtime_error("game.content: Bootstrap requires current authored scene format");
    std::set<AssetId> selected;
    std::vector<AssetId> queue;
    auto collect = [&](const Json& rows) {
        for (const auto& row : rows)
            if (row.contains("prefab_instance"))
                queue.push_back(row.at("prefab_instance").at("asset").get<AssetId>());
    };
    collect(scene.at("entities"));
    Json prefabs = Json::array();
    while (!queue.empty()) {
        const auto id = queue.back();
        queue.pop_back();
        if (!selected.insert(id).second)
            continue;
        if (selected.size() > 16384)
            throw std::runtime_error("game.content: Too many prefab sources");
        auto source = read(id, PrefabAsset::type);
        PrefabDocument::validate(source);
        collect(source.at("members"));
        for (const auto& dependency : source.value("dependencies", Json::array()))
            queue.push_back(dependency.get<AssetId>());
        prefabs.push_back(std::move(source));
    }
    scene["_prefab_sources"] = std::move(prefabs);
    return scene; // Realization/schema/physics checks belong to the candidate world.
}
} // namespace forge
