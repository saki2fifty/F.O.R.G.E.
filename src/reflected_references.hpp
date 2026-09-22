#pragma once
#include "reflected_value.hpp"
#include <forge/identity.hpp>
namespace forge::detail {
namespace reference_detail {
// Input schema/value has already passed bounded admission. Visit only declared
// fields; opaque extensions and AssetRefs are never searched for UUID-like text.
inline void remap(const nlohmann::json& schema, nlohmann::json& value, AssetId source,
                  AssetId destination, const std::map<EntityId, EntityId>& entities) {
    const auto kind = schema.at("type").get<std::string>();
    if (kind == "entity_ref") {
        if (!value.is_null())
            value = remap_entity_ref(value.get<EntityRef>(), source, destination, entities);
    } else if (kind == "struct") {
        for (const auto& field : schema.at("fields"))
            remap(field, value.at(field.at("id").get<std::string>()), source, destination,
                  entities);
    } else if (kind == "array" || kind == "vector") {
        for (auto& element : value)
            remap(schema.at("element"), element, source, destination, entities);
    }
}
} // namespace reference_detail
inline nlohmann::json remap_reflected_entity_refs(const nlohmann::json& schema,
                                                  const nlohmann::json& value, AssetId source,
                                                  AssetId destination,
                                                  const std::map<EntityId, EntityId>& entities) {
    validate_reflected_json(schema, value);
    auto candidate = value;
    reference_detail::remap(schema, candidate, source, destination, entities);
    return candidate;
}
inline void remap_custom_entity_refs(nlohmann::json& row, const nlohmann::json& schema,
                                     AssetId source, AssetId destination,
                                     const std::map<EntityId, EntityId>& entities) {
    using Json = nlohmann::json;
    const auto components_schema = schema.find("components");
    if (components_schema == schema.end())
        return;
    // Borrow the admitted schema; copying every descriptor per entity makes
    // simple edits scale with unrelated component metadata.
    for (const auto& type : *components_schema) {
        if (!type.value("custom", false))
            continue;
        const auto key = type.at("id").get<std::string>();
        for (const char* channel : {"components", "property_overrides"}) {
            if (!row.contains(channel) || !row.at(channel).contains(key))
                continue;
            auto& value = row[channel][key];
            if (!value.is_object() || value.value("$forge", Json()) != type.at("admission"))
                continue;
            if (std::string_view(channel) == "components")
                value =
                    remap_reflected_entity_refs({{"type", "struct"}, {"fields", type.at("fields")}},
                                                value, source, destination, entities);
            else
                for (const auto& field : type.at("fields")) {
                    const auto name = field.at("id").get<std::string>();
                    if (value.contains(name))
                        value[name] = remap_reflected_entity_refs(field, value.at(name), source,
                                                                  destination, entities);
                }
        }
    }
}
} // namespace forge::detail
