#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "runtime_scene_content.hpp"
#include <forge/engine_assets.hpp>
#include <forge/game_content.hpp>
#include <forge/prefab.hpp>
#include <forge/runtime_content_access.hpp>
#include <forge/scene.hpp>
#include <set>
namespace forge {
namespace {
Json prepare_scene(const std::filesystem::path& root, AssetRef<SceneAsset> ref,
                   const Json* snapshot) {
    const RuntimeContentAccess access(root);
    const auto catalog = AssetCatalog::open_project(root);
    std::size_t bytes = snapshot ? snapshot->dump().size() : 0;
    if (bytes > 64 * 1024 * 1024)
        throw std::runtime_error("game.content: Initial snapshot exceeds 64 MiB");
    auto read = [&](AssetId id, std::string_view type) {
        const auto found = catalog.records().find(id);
        if (found == catalog.records().end() || found->second.type != type ||
            found->second.subasset)
            throw std::runtime_error("game.content: Missing or incompatible " + std::string(type) +
                                     " " + id.str());
        const auto version = found->second.schema_version;
        if (type == SceneAsset::type ? version < 3 || version > 5 : version < 1 || version > 3)
            throw std::runtime_error("game.content: Unsupported catalog schema version");
        auto data = access.read(found->second.source, 64 * 1024 * 1024);
        if (data.size() > 256 * 1024 * 1024 - bytes)
            throw std::runtime_error("game.content: Scene/prefab closure exceeds 256 MiB");
        bytes += data.size();
        auto doc = asset_detail::parse_bounded_json(data, 64 * 1024 * 1024);
        if (doc.at("asset_id").get<AssetId>() != id || doc.at("version") != version)
            throw std::runtime_error(
                "game.content: Scene/prefab identity/schema differs from catalog");
        return doc;
    };
    auto scene = snapshot ? *snapshot : read(ref.id, SceneAsset::type);
    PrefabSources provided;
    if (snapshot) {
        for (const auto& source : scene.value("_prefab_sources", Json::array())) {
            const PrefabDocument definition(source);
            if (!provided.emplace(definition.asset(), source).second)
                throw std::runtime_error("game.content: Duplicate snapshot Prefab source");
        }
        scene.erase("_prefab_sources");
    }
    Scene::validate_document(scene);
    if (scene.at("version") < 3 || scene.contains("_prefab_sources"))
        throw std::runtime_error("game.content: Bootstrap requires current authored scene format");
    std::set<AssetId> selected;
    std::vector<AssetId> queue;
    for (const auto& [id, source] : provided) {
        (void)source;
        queue.push_back(id);
    }
    auto collect = [&](const Json& rows) {
        for (const auto& row : rows)
            if (row.contains("prefab_instance"))
                queue.push_back(row.at("prefab_instance").at("asset").get<AssetId>());
    };
    collect(scene.at("entities"));
    // Code-selected Prefabs have no authored instance to discover. Traverse the
    // same typed Runtime graph that export uses, including module declarations.
    // This reads existing metadata only; it neither imports nor scans directories.
    std::set<AssetId> reachable;
    std::vector<AssetId> dependencies{ref.id};
    while (!dependencies.empty()) {
        const auto id = dependencies.back();
        dependencies.pop_back();
        if (!reachable.insert(id).second)
            continue;
        if (reachable.size() > 16384)
            throw std::runtime_error("game.content: Runtime dependency closure exceeds limit");
        for (const auto& edge : catalog.dependency_graph().dependencies(id)) {
            if (edge.kind != AssetDependencyKind::Runtime || edge.role == "ui.observed")
                continue;
            const auto found = catalog.records().find(edge.target);
            const auto* builtin = engine_asset(edge.target);
            if (builtin ? builtin->type != edge.expected_type
                        : found == catalog.records().end() ||
                              found->second.type != edge.expected_type ||
                              (found->second.subasset && found->second.subasset->removed))
                throw std::runtime_error("game.content: Missing/incompatible Runtime dependency");
            if (edge.expected_type == PrefabAsset::type)
                queue.push_back(edge.target);
            if (!builtin)
                dependencies.push_back(edge.target);
        }
    }
    Json prefabs = Json::array();
    while (!queue.empty()) {
        const auto id = queue.back();
        queue.pop_back();
        if (!selected.insert(id).second)
            continue;
        if (selected.size() > 16384)
            throw std::runtime_error("game.content: Too many prefab sources");
        auto source = provided.contains(id) ? provided.at(id) : read(id, PrefabAsset::type);
        PrefabDocument::validate(source);
        collect(source.at("members"));
        for (const auto& dependency : source.value("dependencies", Json::array()))
            queue.push_back(dependency.get<AssetId>());
        prefabs.push_back(std::move(source));
    }
    scene["_prefab_sources"] = std::move(prefabs);
    return scene; // Realization/schema/physics checks belong to the candidate world.
}
} // namespace
Json load_game_scene(const std::filesystem::path& root, AssetRef<SceneAsset> ref) {
    return prepare_scene(root, ref, nullptr);
}
Json detail::runtime_scene_content(const std::filesystem::path& root, const Json& snapshot) {
    // Older caller snapshots retain their existing migration/admission path;
    // without durable Scene identity they cannot own catalog declarations.
    if (!snapshot.contains("asset_id") || snapshot.value("version", 0u) < 3)
        return snapshot;
    return prepare_scene(root, {snapshot.at("asset_id").get<AssetId>()}, &snapshot);
}
} // namespace forge
