#pragma once
#include "builtins.hpp"
#include <algorithm>
#include <functional>
#include <set>

namespace forge::detail {
// Definition-boundary normalization only. Native storage always holds EntityRef.
// The schema, never the spelling of an unknown JSON value, admits a reference.
inline void transform_prefab_reference_field(const Json& field, Json& value,
                                             const std::function<Json(const Json&)>& transform,
                                             bool& found) {
    const auto type = field.at("type").get<std::string>();
    if (type == "entity_ref") {
        if (!value.is_null()) {
            value = transform(value);
            found = true;
        }
    } else if (type == "struct") {
        if (!value.is_object())
            throw std::runtime_error("Prefab reflected structure must be an object");
        for (const auto& child : field.at("fields")) {
            const auto key = child.at("id").get<std::string>();
            if (value.contains(key))
                transform_prefab_reference_field(child, value[key], transform, found);
        }
    } else if (type == "array" || type == "vector") {
        if (!value.is_array())
            throw std::runtime_error("Prefab reflected collection must be an array");
        for (auto& element : value)
            transform_prefab_reference_field(field.at("element"), element, transform, found);
    }
}
inline const Json& prefab_reference_schema(const Json& schema) {
    if (schema.contains("components"))
        return schema;
    static const Json builtins = builtin_schema();
    return builtins;
}
inline bool prefab_reference_field(const Json& field) {
    const auto type = field.at("type").get<std::string>();
    if (type == "entity_ref")
        return true;
    if (type == "array" || type == "vector")
        return prefab_reference_field(field.at("element"));
    if (type == "struct")
        for (const auto& child : field.at("fields"))
            if (prefab_reference_field(child))
                return true;
    return false;
}
inline std::set<std::string>
transform_prefab_component_references(Json& components, const Json& schema,
                                      const std::function<Json(const Json&)>& transform) {
    const auto& types = prefab_reference_schema(schema).at("components");
    std::set<std::string> found;
    for (const auto& type : types) {
        const auto key = type.at("id").get<std::string>();
        if (!components.contains(key) || !components.at(key).is_object())
            continue;
        auto& value = components[key];
        if (type.value("custom", false) && value.value("$forge", Json()) != type.at("admission"))
            continue;
        bool contains_reference = false;
        for (const auto& field : type.at("fields")) {
            const auto name = field.at("id").get<std::string>();
            if (value.contains(name))
                transform_prefab_reference_field(field, value[name], transform, contains_reference);
        }
        if (contains_reference)
            found.insert(key);
    }
    return found;
}
inline PrefabMemberRef checked_prefab_reference(const Json& value, AssetId asset) {
    if (!value.is_object() || value.contains("scene") || value.contains("entity"))
        throw std::runtime_error("Prefab v3 references require a source member or null");
    const auto reference = value.get<PrefabMemberRef>();
    if (reference.prefab != asset)
        throw std::runtime_error("Prefab member reference belongs to another source");
    return reference;
}
inline Json normalize_prefab_components(
    const Json& source, const Json& member, const Json& schema,
    const std::function<Json(PrefabMemberRef)>& resolve = [](PrefabMemberRef) { return Json(); }) {
    auto values = member.at("components");
    if (source.at("version") == 3) {
        const auto asset = source.at("asset_id").get<AssetId>();
        transform_prefab_component_references(values, schema, [&](const Json& value) {
            auto result = resolve(checked_prefab_reference(value, asset));
            if (!result.is_null())
                for (const auto& [key, extension] : value.items())
                    if (key != "prefab" && key != "member")
                        result[key] = extension;
            return result;
        });
    }
    return values;
}
inline Json capture_prefab_reference(const Json& value, AssetId scene, AssetId prefab,
                                     const std::map<std::string, std::string>& members) {
    if (value.contains("prefab") || value.contains("member"))
        throw std::runtime_error(
            "Prefab reference conversion conflicts with reserved identity fields; "
            "the original value has not been changed");
    const auto reference = value.get<EntityRef>();
    if (reference.scene != scene || !members.contains(reference.entity.str()))
        throw std::runtime_error(
            "Prefab component reference points outside this subtree/instance; clear it or "
            "include its target before capture/Apply");
    auto result = value;
    result.erase("scene");
    result.erase("entity");
    result["prefab"] = prefab;
    result["member"] = members.at(reference.entity.str());
    return result;
}
inline std::map<std::string, std::set<std::string>>
prefab_native_materializations(const Json& scene, const std::map<AssetId, Json>& sources,
                               const Json& schema,
                               std::map<std::string, Json>* baselines = nullptr) {
    if (sources.empty())
        return {};
    std::map<std::string, const Json*> roots;
    for (const auto& row : scene.at("entities"))
        if (row.contains("prefab_instance"))
            roots.emplace(row.at("id").get<std::string>(), &row);
    if (roots.empty())
        return {};
    std::map<std::string, std::set<std::string>> result;
    for (const auto& row : scene.at("entities")) {
        const Json* root = nullptr;
        std::string member;
        if (row.contains("prefab_instance"))
            root = &row;
        else if (row.contains("prefab_member")) {
            root = roots.at(row.at("prefab_member").at("root").get<std::string>());
            member = row.at("prefab_member").at("member");
        }
        if (!root || row.value("missing_member", false))
            continue;
        const auto source = sources.find(root->at("prefab_instance").at("asset").get<AssetId>());
        if (source == sources.end())
            continue;
        const bool portable = source->second.at("version") == 3;
        const auto masks = row.find("property_overrides");
        if (!portable && (masks == row.end() || masks->empty()))
            continue;
        if (member.empty())
            member = source->second.at("root");
        for (const auto& definition : source->second.at("members"))
            if (definition.at("id") == member) {
                auto components = definition.at("components");
                auto keys = portable ? transform_prefab_component_references(
                                           components, schema, [](const Json&) { return Json(); })
                                     : std::set<std::string>{};
                // Explicit whole values need valid copied projection seeds;
                // partial masks already materialize native storage. Clearing a
                // source reference must preserve both kinds of ownership, and
                // later C++ changes outside a mask must reach recovery.
                if (row.contains("property_overrides") || !row.at("components").empty())
                    for (const auto& type : prefab_reference_schema(schema).at("components")) {
                        const auto key = type.at("id").get<std::string>();
                        const bool whole = portable && row.at("components").contains(key);
                        const bool partial = masks != row.end() && masks->contains(key);
                        if (!whole && !partial)
                            continue;
                        if (type.value("custom", false)) {
                            if (partial &&
                                (!masks->at(key).is_object() ||
                                 masks->at(key).value("$forge", Json()) != type.at("admission")))
                                continue;
                            if (whole && (!row.at("components").at(key).is_object() ||
                                          row.at("components").at(key).value("$forge", Json()) !=
                                              type.at("admission")))
                                continue;
                            if (partial && components.contains(key) &&
                                (!components.at(key).is_object() ||
                                 components.at(key).value("$forge", Json()) !=
                                     type.at("admission")))
                                continue;
                        }
                        // A mask already owns native storage. Compare every
                        // unmasked field too, so unrelated runtime creation or
                        // recovery cannot replay inherited values over C++ edits.
                        if (partial) {
                            if (!components.contains(key)) {
                                if (type.value("custom", false)) {
                                    components[key] = type.at("defaults");
                                    components[key]["$forge"] = type.at("admission");
                                } else {
                                    const auto builtin = std::find_if(
                                        builtins().begin(), builtins().end(),
                                        [&](const auto& value) { return key == value.name; });
                                    if (builtin == builtins().end())
                                        continue;
                                    components[key] = builtin->defaults;
                                }
                            }
                            keys.insert(key);
                        }
                        for (const auto& field : type.at("fields"))
                            if (prefab_reference_field(field)) {
                                keys.insert(key);
                                break;
                            }
                    }
                if (!keys.empty()) {
                    if (baselines) {
                        auto normalized =
                            portable
                                ? normalize_prefab_components(
                                      source->second, definition, schema,
                                      [&](PrefabMemberRef reference) {
                                          return Json(EntityRef{scene.at("asset_id").get<AssetId>(),
                                                                root->at("prefab_instance")
                                                                    .at("members")
                                                                    .at(reference.member.str())
                                                                    .get<EntityId>()});
                                      })
                                : definition.at("components");
                        for (const auto& key : keys)
                            if (!normalized.contains(key))
                                normalized[key] = components.at(key);
                        baselines->emplace(row.at("id").get<std::string>(), std::move(normalized));
                    }
                    result.emplace(row.at("id").get<std::string>(), std::move(keys));
                }
                break;
            }
    }
    return result;
}
} // namespace forge::detail
