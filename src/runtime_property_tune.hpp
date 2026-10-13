#pragma once
#include <forge/identity.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
namespace forge {
// Shared shape/identity checks for the existing private editor command. World
// generations come from each host's existing successful-replacement owner.
struct RuntimePropertyTune {
    EntityRef target;
    std::string type, property;
    static bool navigation_type(std::string_view type) {
        return type == "forge.navigation_avoidance";
    }
    static RuntimePropertyTune parse(const nlohmann::json& request, std::uint64_t generation,
                                     AssetId scene) {
        if (!generation || !request.contains("generation") ||
            !request.at("generation").is_number_unsigned() ||
            request.at("generation").get<std::uint64_t>() != generation)
            throw std::runtime_error("Live property targets a stale world generation");
        for (const char* key : {"scene_asset", "entity", "type", "property"})
            if (!request.contains(key) || !request.at(key).is_string())
                throw std::runtime_error("Live property request needs typed target and value");
        if (!request.contains("value"))
            throw std::runtime_error("Live property request needs typed target and value");
        RuntimePropertyTune result{
            {request.at("scene_asset").get<AssetId>(), request.at("entity").get<EntityId>()},
            request.at("type"),
            request.at("property")};
        if (result.type.empty() || result.type.size() > 128 || result.property.empty() ||
            result.property.size() > 128 || request.at("value").dump().size() > 8192)
            throw std::runtime_error("Live property target/value exceeds limits");
        if (result.target.scene != scene)
            throw std::runtime_error("Live property targets a different scene asset");
        return result;
    }
};
} // namespace forge
