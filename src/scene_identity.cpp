#include "reflected_references.hpp"
#include "spatial_document.hpp"
#include <forge/scene.hpp>
#include <forge/scene_identity.hpp>
#include <fstream>
#include <set>
namespace forge {
namespace {
Json read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read scene identity/source: " + path.string());
    return Json::parse(stream);
}
std::filesystem::path sibling(const std::filesystem::path& path, const char* suffix) {
    auto result = path;
    result += suffix;
    return result;
}
} // namespace
Json empty_scene() {
    return {{"version", 3}, {"asset_id", AssetId::generate()}, {"entities", Json::array()}};
}
std::string resolve_legacy_id(const Json& document, const std::string& id) {
    if (document.contains("legacy_ids") && document.at("legacy_ids").contains(id))
        return document.at("legacy_ids").at(id).get<std::string>();
    return id;
}
static Json migrate_identity(const Json& source, const Json* existing = nullptr) {
    Scene::validate_document(source);
    if (source.at("version") == 2)
        return source;
    if (source.contains("asset_id") || source.contains("legacy_ids"))
        throw std::runtime_error(
            "Legacy scene uses reserved v2 identity fields; original preserved");
    auto result = source;
    result["version"] = 2;
    result["asset_id"] = existing ? existing->at("asset_id") : Json(AssetId::generate());
    auto aliases = existing ? existing->value("legacy_ids", Json::object()) : Json::object();
    std::set<std::string> occupied;
    for (const auto& value : aliases)
        occupied.insert(value.get<std::string>());
    for (const auto& entity : source.at("entities")) {
        const auto old = entity.at("id").get<std::string>();
        if (!aliases.contains(old)) {
            std::string id;
            do {
                id = EntityId::generate().str();
            } while (!occupied.insert(id).second);
            aliases[old] = id;
        }
    }
    for (auto& entity : result["entities"]) {
        entity["id"] = aliases.at(entity.at("id").get<std::string>());
        for (const char* relation : {"parent", "base"})
            if (entity.contains(relation))
                entity[relation] = aliases.at(entity.at(relation).get<std::string>());
    }
    result["legacy_ids"] = std::move(aliases);
    Scene::validate_document(result);
    return result;
}
Json migrate_scene(const Json& source, const Json* existing) {
    if (source.at("version") == 3 || source.at("version") == 4 || source.at("version") == 5) {
        Scene::validate_document(source);
        auto compatible = source;
        detail::promote_scale_format(compatible, "entities", 5);
        return compatible;
    }
    auto result = detail::migrate_transforms(migrate_identity(source, existing));
    Scene::validate_document(result);
    return result;
}
Json duplicate_scene_asset(const Json& source) {
    return duplicate_scene_asset(source, AssetId::generate());
}
Json duplicate_scene_asset(const Json& source, AssetId destination) {
    return duplicate_scene_asset(source, destination, Json::object());
}
Json duplicate_scene_asset(const Json& source, AssetId destination, const Json& schema) {
    Scene::validate_document(source);
    if (source.at("version") != 3 && source.at("version") != 4 && source.at("version") != 5)
        throw std::runtime_error("Migrate the scene before duplicating its asset");
    if (!destination || destination == source.at("asset_id").get<AssetId>())
        throw std::runtime_error("Duplicated scene requires a new asset identity");
    auto result = source;
    result["asset_id"] = destination;
    std::map<std::string, std::string> remap;
    std::set<std::string> allocated;
    for (const auto& entity : source.at("entities"))
        allocated.insert(entity.at("id"));
    // Retain missing legacy targets as missing, without aliasing the source's entities.
    const auto source_aliases = source.value("legacy_ids", Json::object());
    for (const auto& [alias, id] : source_aliases.items()) {
        (void)alias;
        allocated.insert(id.get<std::string>());
    }
    auto fresh = [&](const std::string& old) {
        if (!remap.contains(old)) {
            std::string id;
            do {
                id = EntityId::generate().str();
            } while (!allocated.insert(id).second);
            remap[old] = id;
        }
        return remap.at(old);
    };
    for (auto& entity : result["entities"])
        entity["id"] = fresh(entity.at("id"));
    for (const auto& entity : result["entities"])
        if (entity.contains("prefab_instance"))
            for (const auto& id : entity.at("prefab_instance").at("members"))
                (void)fresh(id.get<std::string>());
    std::map<EntityId, EntityId> typed_remap;
    for (const auto& [old, id] : remap)
        typed_remap.emplace(EntityId::parse(old), EntityId::parse(id));
    for (auto& entity : result["entities"])
        for (const char* relation : {"parent", "base"})
            if (entity.contains(relation))
                entity[relation] =
                    remap_entity_ref(
                        {source.at("asset_id").get<AssetId>(), entity.at(relation).get<EntityId>()},
                        source.at("asset_id").get<AssetId>(), result.at("asset_id").get<AssetId>(),
                        typed_remap)
                        .entity;
    for (auto& e : result["entities"])
        detail::remap_spatial(e, source.at("asset_id").get<AssetId>(),
                              result.at("asset_id").get<AssetId>(), typed_remap);
    remap_prefab_instances(result, typed_remap);
    for (auto& entity : result["entities"])
        detail::remap_custom_entity_refs(entity, schema, source.at("asset_id").get<AssetId>(),
                                         destination, typed_remap);
    if (result.contains("legacy_ids"))
        for (auto& id : result["legacy_ids"])
            id = fresh(id.get<std::string>());
    Scene::validate_document(result);
    return result;
}
Json read_scene_file(const std::filesystem::path& path) {
    auto source = read(path);
    Scene::validate_document(source);
    if (source.at("version") != 1)
        return migrate_scene(source);
    const auto journal_path = sibling(path, ".forge-identity.json");
    if (std::filesystem::exists(journal_path)) {
        const auto journal = read(journal_path);
        if (journal.at("version") != 1 || journal.at("source") != source)
            throw std::runtime_error(
                "Legacy scene differs from its identity record: " + path.string() +
                ". Preserve both files and resolve the conflict before migration.");
        const auto result = journal.at("document");
        Scene::validate_document(result);
        if (migrate_identity(source, &result) != result)
            throw std::runtime_error("Invalid scene identity record: " + journal_path.string());
        return migrate_scene(result);
    }
    const auto result = migrate_identity(source);
    atomic_write(journal_path,
                 Json{{"version", 1}, {"source", source}, {"document", result}}.dump(2));
    return migrate_scene(result);
}
void write_scene_file(const std::filesystem::path& path, const Json& document) {
    Scene::validate_document(document);
    if (document.at("version") != 3 && document.at("version") != 4 && document.at("version") != 5)
        throw std::runtime_error("Save requires a migrated scene");
    if (std::filesystem::exists(path)) {
        const auto source = read(path);
        if (source.at("version") == 1 || source.at("version") == 2) {
            const auto backup =
                sibling(path, source.at("version") == 1 ? ".v1.backup" : ".v2.backup");
            if (std::filesystem::exists(backup)) {
                if (read(backup) != source)
                    throw std::runtime_error("Legacy backup differs; save aborted: " +
                                             backup.string());
            } else {
                // Preserve the original bytes, including unknown fields and formatting.
                std::string original;
                {
                    std::ifstream stream(path, std::ios::binary);
                    if (!stream)
                        throw std::runtime_error("Cannot read original scene for backup");
                    original.assign(std::istreambuf_iterator<char>(stream), {});
                    if (stream.bad())
                        throw std::runtime_error("Cannot finish reading scene backup");
                }
                // Publish the backup atomically too: interruption cannot leave a partial
                // final backup that would make every later retry fail validation.
                atomic_write(backup, original);
            }
        }
    }
    auto compatible = document;
    detail::promote_scale_format(compatible, "entities", 5);
    atomic_write(path, compatible.dump(2));
}
} // namespace forge
