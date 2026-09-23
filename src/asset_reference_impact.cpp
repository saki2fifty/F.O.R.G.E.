#include "asset_reference_impact.hpp"
#include "asset_bytes.hpp"
#include "asset_storage.hpp"
#include "bounded_json.hpp"
#include <forge/material_source.hpp>
#include <forge/scene_render_settings.hpp>
namespace forge {
namespace {
using Json = nlohmann::json;
constexpr std::size_t max_entries = 65536;
struct Inspect {
    const std::set<AssetId>& targets;
    AssetReferenceImpact result;
    std::map<std::string, Json> types;
    std::size_t visits = 0;
    std::filesystem::path source;
    std::string owner;
    bool all = false;
    void budget(unsigned depth = 0) {
        if (++visits > 1048576 || depth > 32)
            throw std::runtime_error("Reference inspection exceeds its node/depth budget");
    }
    void unknown(const std::string& field) {
        if (result.uninspected.size() >= max_entries)
            throw std::runtime_error("Reference inspection has too many opaque fields");
        result.uninspected.push_back(path_utf8(source) + " / " + owner + " / " + field);
    }
    void reference(const Json& value, const std::string& property, const char* kind,
                   const std::string& expected_type) {
        if (value.is_null())
            return;
        const auto id = value.get<AssetId>();
        if (all || targets.contains(id)) {
            if (result.references.size() >= max_entries)
                throw std::runtime_error("Reference impact exceeds 65536 matching references");
            result.references.push_back({source, owner, property, kind, id, expected_type});
        }
    }
    void fields(const Json& descriptions, const Json& value, const std::string& path,
                unsigned depth, bool admission = false) {
        if (!value.is_object())
            throw std::runtime_error("Expected object in reflected reference inspection");
        std::set<std::string> known;
        for (const auto& field : descriptions) {
            budget(depth);
            const auto id = field.at("id").get<std::string>();
            known.insert(id);
            if (value.contains(id))
                visit(field, value.at(id), path.empty() ? id : path + "." + id, depth + 1);
        }
        for (const auto& [id, unused] : value.items()) {
            (void)unused;
            if (!known.contains(id) && !(admission && id == "$forge"))
                unknown(path + "." + id);
        }
    }
    void visit(const Json& field, const Json& value, const std::string& path, unsigned depth) {
        budget(depth);
        const auto type = field.at("type").get<std::string>();
        if (type == "asset_ref")
            reference(value, path, "AssetRef", field.at("asset_type").get<std::string>());
        else if (type == "entity_ref") {
            if (!value.is_null())
                reference(value.at("scene"), path, "EntityRef scene", SceneAsset::type);
        } else if (type == "struct")
            fields(field.at("fields"), value, path, depth + 1);
        else if (type == "array" || type == "vector") {
            if (!value.is_array())
                throw std::runtime_error("Expected reflected collection in reference inspection");
            for (std::size_t i = 0; i < value.size(); ++i)
                visit(field.at("element"), value[i], path + "[" + std::to_string(i) + "]",
                      depth + 1);
        } else if (type != "bool" && type != "string" && type != "enum" && type != "bitmask" &&
                   type != "float32" && type != "float64" && type != "int8" && type != "uint8" &&
                   type != "int16" && type != "uint16" && type != "int32" && type != "uint32" &&
                   type != "int64" && type != "uint64" && type != "char")
            unknown(path + " (unsupported reflected type " + type + ")");
    }
    void document(const AssetReferenceDocument& input) {
        source = input.source;
        owner.clear();
        const auto& doc = input.document;
        if (!doc.is_object()) {
            unknown("untyped JSON document");
            return;
        }
        const bool scene =
            doc.contains("version") && doc.contains("entities") && doc.at("entities").is_array();
        const bool prefab = doc.contains("format") && doc.at("format").is_string() &&
                            doc.at("format").get<std::string>() == "forge.prefab";
        const auto rows = scene ? "entities" : prefab ? "members" : nullptr;
        if (rows) {
            for (const auto& row : doc.at(rows)) {
                budget();
                owner = row.value("name", row.value("id", std::string{}));
                for (const auto* channel : {"components", "property_overrides"}) {
                    const auto values = row.value(channel, Json::object());
                    for (const auto& [component, value] : values.items()) {
                        const auto found = types.find(component);
                        const bool custom =
                            found != types.end() && found->second.value("custom", false);
                        if (found == types.end() ||
                            (custom && (!value.is_object() || value.value("$forge", Json()) !=
                                                                  found->second.at("admission"))))
                            unknown(component);
                        else
                            fields(found->second.at("fields"), value, component, 0, custom);
                    }
                }
                if (row.contains("prefab_instance"))
                    reference(row.at("prefab_instance").at("asset"), "prefab_instance.asset",
                              "Prefab instance", PrefabAsset::type);
                if (row.contains("spatial") && row.at("spatial").contains("target"))
                    reference(row.at("spatial").at("target").at("scene"), "spatial.target",
                              "EntityRef scene", SceneAsset::type);
                // Both whole-component and per-property intent carry authored values.
                for (const auto& [key, unused] : row.items()) {
                    (void)unused;
                    if (key != "id" && key != "name" && key != "components" && key != "parent" &&
                        key != "base" && key != "prefab" && key != "spatial" &&
                        key != "prefab_instance" && key != "prefab_member" &&
                        key != "property_overrides")
                        unknown(key);
                }
            }
            owner.clear();
            if (scene && doc.contains("rendering")) {
                const auto rendering = scene_render_settings(doc);
                if (rendering.environment.texture.id)
                    reference(Json(rendering.environment.texture.id),
                              "rendering.environment.texture", "Environment texture",
                              TextureAsset::type);
                for (const auto& [key, unused] : doc.at("rendering").items()) {
                    (void)unused;
                    if (key != "version" && key != "exposure" && key != "environment" &&
                        key != "shadows")
                        unknown("rendering." + key);
                }
                for (const auto* section : {"environment", "shadows"}) {
                    if (!doc.at("rendering").contains(section))
                        continue;
                    const std::set<std::string> known =
                        std::string_view(section) == "environment"
                            ? std::set<std::string>{"texture", "intensity", "rotation", "sky"}
                            : std::set<std::string>{"enabled", "resolution", "cascades",
                                                    "max_lights", "distance"};
                    for (const auto& [key, unused] : doc.at("rendering").at(section).items()) {
                        (void)unused;
                        if (!known.contains(key))
                            unknown(std::string("rendering.") + section + "." + key);
                    }
                }
            }
            if (prefab)
                for (const auto& dependency : doc.value("dependencies", Json::array()))
                    reference(dependency, "dependencies", "Prefab dependency", PrefabAsset::type);
            for (const auto& [key, unused] : doc.items()) {
                (void)unused;
                if (key != "format" && key != "version" && key != "asset_id" && key != "entities" &&
                    key != "members" && key != "revision" && key != "root" && key != "legacy_ids" &&
                    key != "rendering" && !(prefab && key == "dependencies"))
                    unknown(key);
            }
        } else if (doc.contains("kind") && doc.at("kind").is_string() &&
                   doc.at("kind").get<std::string>() == "forge.material") {
            MaterialSource material{doc};
            material.validate();
            if (const auto base = material.base())
                reference(Json(base->id), "base", "Material base", MaterialAsset::type);
            if (doc.contains("overrides") && doc.at("overrides").contains("textures"))
                for (const auto& [key, binding] : doc.at("overrides").at("textures").items())
                    if (!binding.is_null())
                        reference(binding.at("asset"), "textures." + key, "Material texture",
                                  TextureAsset::type);
            for (const auto& [key, unused] : doc.items()) {
                (void)unused;
                if (key != "kind" && key != "version" && key != "asset_id" && key != "base" &&
                    key != "overrides")
                    unknown(key);
            }
            for (const auto& [key, unused] : doc.at("overrides").items()) {
                (void)unused;
                if (key != "model" && key != "alpha" && key != "alpha_cutoff" &&
                    key != "double_sided" && key != "depth_test" && key != "depth_write" &&
                    key != "parameters" && key != "textures")
                    unknown("overrides." + key);
            }
        } else if (doc.contains("startup_scene")) {
            const auto& startup = doc.at("startup_scene");
            if (startup.is_object())
                reference(startup.at("asset"), "startup_scene", "Project startup scene",
                          SceneAsset::type);
            else if (!startup.is_null())
                unknown("legacy startup locator");
        } else
            unknown("no reference adapter for this document");
    }
};
} // namespace
AssetReferenceImpact collect_asset_references(const Json& schema,
                                              std::span<const AssetReferenceDocument> documents) {
    const std::set<AssetId> unused;
    Inspect scan{unused, {}, {}};
    scan.all = true;
    for (const auto& component : schema.at("components"))
        if (!scan.types.emplace(component.at("id").get<std::string>(), component).second)
            throw std::runtime_error("Duplicate component in reference inspection schema");
    for (const auto& doc : documents)
        scan.document(doc);
    return std::move(scan.result);
}
AssetReferenceImpact inspect_asset_references(const Json& schema,
                                              std::span<const AssetReferenceDocument> documents,
                                              const std::set<AssetId>& targets) {
    Inspect scan{targets, {}, {}};
    for (const auto& component : schema.at("components"))
        if (!scan.types.emplace(component.at("id").get<std::string>(), component).second)
            throw std::runtime_error("Duplicate component in reference inspection schema");
    for (const auto& doc : documents) {
        try {
            scan.document(doc);
        } catch (const std::exception& e) {
            throw std::runtime_error("Cannot inspect references in " + path_utf8(doc.source) +
                                     ": " + e.what());
        }
    }
    return std::move(scan.result);
}
AssetReferenceImpact scan_asset_references(const std::filesystem::path& project, const Json& schema,
                                           const std::set<AssetId>& targets,
                                           std::span<const AssetReferenceDocument> drafts,
                                           std::stop_token stop) {
    SourceScanOptions options;
    options.include_project_root = true;
    const auto snapshot = scan_asset_sources(project, options, stop);
    if (!snapshot.complete)
        throw std::runtime_error("Reference scan incomplete; file operation has not been approved");
    const ProjectPaths paths(project);
    Inspect inspector{targets, {}, {}};
    for (const auto& component : schema.at("components"))
        if (!inspector.types.emplace(component.at("id").get<std::string>(), component).second)
            throw std::runtime_error("Duplicate component in reference inspection schema");
    std::map<std::filesystem::path, std::string> reviewed;
    std::size_t bytes = 0;
    for (const auto& [path, file] : snapshot.files) {
        if (stop.stop_requested())
            throw std::runtime_error("Reference inspection cancelled");
        auto extension = path.extension().string();
        for (auto& c : extension)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        if (extension != ".json" || !file.alias_of.empty() || path == "forge.assets.json")
            continue;
        if (file.bytes > 64 * 1024 * 1024 || file.bytes > 256 * 1024 * 1024 - bytes)
            throw std::runtime_error(
                "Reference scan exceeds 64 MiB document / 256 MiB total limit");
        bytes += file.bytes;
        auto data = asset_storage::read(paths.resolve(path), 64 * 1024 * 1024);
        if (!data || asset_detail::content_digest(std::as_bytes(std::span(*data))) != file.digest)
            throw std::runtime_error("Source changed during reference inspection: " +
                                     path_utf8(path));
        reviewed.emplace(path, file.digest);
        // A non-authored JSON source may be an importer input; report opaque coverage.
        Json doc;
        try {
            doc =
                asset_detail::parse_bounded_json(std::as_bytes(std::span(*data)), 64 * 1024 * 1024);
        } catch (const std::exception&) {
            if (file.source_kind == "scene" || file.source_kind == "prefab" ||
                file.source_kind == "material" || path == "forge.project.json")
                throw std::runtime_error("Authored reference source cannot be parsed: " +
                                         path_utf8(path));
            inspector.source = path;
            inspector.owner.clear();
            inspector.unknown("unparsed JSON; cannot inspect references");
            continue;
        }
        inspector.document({path, std::move(doc)});
    }
    for (const auto& draft : drafts)
        inspector.document(draft);
    inspector.result.reviewed_sources = std::move(reviewed);
    return std::move(inspector.result);
}
} // namespace forge
