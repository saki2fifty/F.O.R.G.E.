#include <algorithm>
#include <forge/assets.hpp>
#include <forge/project.hpp>
#include <forge/scene.hpp>
#include <forge/schema.hpp>
namespace forge {
void SchemaRegistry::add(DocumentSchema s) {
    if (s.id.empty() || !s.current_version || s.readable_versions.empty() || !s.validate ||
        !s.migrate || schemas_.contains(s.id))
        throw std::runtime_error("Invalid or duplicate document schema registration");
    const auto id = s.id;
    schemas_.emplace(id, std::move(s));
}
Json SchemaRegistry::describe() const {
    Json result = Json::array();
    for (const auto& [id, s] : schemas_)
        result.push_back({{"id", id},
                          {"current_version", s.current_version},
                          {"readable_versions", s.readable_versions},
                          {"migration_policy", s.migration_policy}});
    return result;
}
Json SchemaRegistry::prepare(const std::string& kind, const Json& source) const {
    const auto found = schemas_.find(kind);
    if (found == schemas_.end())
        throw std::runtime_error("Unknown document schema: " + kind);
    const auto& s = found->second;
    if (!source.is_object() || !source.contains("version") ||
        !source.at("version").is_number_integer() || source.at("version").get<double>() < 1 ||
        source.at("version").get<double>() > 4294967295.0)
        throw std::runtime_error("Invalid document version");
    const auto version = source.at("version").get<unsigned>();
    if (std::find(s.readable_versions.begin(), s.readable_versions.end(), version) ==
        s.readable_versions.end())
        throw std::runtime_error("Unsupported " + kind + " version");
    s.validate(source);
    auto candidate = s.migrate(source);
    s.validate(candidate);
    return candidate;
}
SchemaRegistry core_document_schemas() {
    SchemaRegistry registry;
    registry.add({"scene",
                  4,
                  {1, 2, 3, 4},
                  "Detached migration of 1/2 to3; preserve3/4. Disk identity journals/backups "
                  "remain read_scene_file/save_scene_file owned.",
                  Scene::validate_document,
                  [](const Json& j) { return migrate_scene(j); }});
    registry.add({"prefab",
                  1,
                  {1},
                  "No historical migration; immutable source candidate validation.",
                  PrefabDocument::validate,
                  [](const Json& j) { return j; }});
    registry.add({"input",
                  1,
                  {1},
                  "No historical migration; stable action IDs and unknown payload preservation.",
                  [](const Json& j) { (void)InputMap(j); },
                  [](const Json& j) { return j; }});
    registry.add({"project",
                  2,
                  {2},
                  "ProjectSettings owns path-dependent v1 startup-identity resolution and backup "
                  "on explicit save; detached v2 validation here.",
                  ProjectSettings::validate,
                  [](const Json& j) { return j; }});
    registry.add(
        {"asset_index",
         2,
         {1, 2},
         "Metadata schema only; project-confined locators and duplicate registration "
         "validated by AssetCatalog.",
         [](const Json& j) {
             if ((j.at("version") != 1 && j.at("version") != 2) || !j.at("assets").is_array() ||
                 j.at("assets").size() > 100000)
                 throw std::runtime_error("Invalid asset index");
             for (const auto& r : j.at("assets")) {
                 (void)r.at("id").get<AssetId>();
                 if (r.at("type").get<std::string>().empty())
                     throw std::runtime_error("Empty asset type");
                 (void)ProjectPaths::normalize(
                     std::filesystem::u8path(r.at("source").get<std::string>()));
                 if (!r.at("schema_version").is_number_integer() ||
                     r.at("schema_version").get<double>() < 1 ||
                     r.at("schema_version").get<double>() > 4294967295.0)
                     throw std::runtime_error("Invalid asset schema version");
                 (void)r.at("dependencies").get<std::vector<AssetId>>();
                 if (r.contains("dependency_edges") && !r.at("dependency_edges").is_array())
                     throw std::runtime_error("Invalid typed asset dependency list");
                 if (r.contains("source_dependencies") && !r.at("source_dependencies").is_array())
                     throw std::runtime_error("Invalid raw source dependency list");
             }
         },
         [](const Json& j) {
             auto result = j;
             result["version"] = 2;
             return result;
         }});
    return registry;
}
} // namespace forge
