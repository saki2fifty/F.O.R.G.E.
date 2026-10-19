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
    static bool particle_type(std::string_view type) { return type == "forge.particle_emitter"; }
    static bool material_type(std::string_view type) { return type == "forge.mesh_renderer"; }
    static bool sprite_type(std::string_view type) { return type == "forge.sprite_renderer"; }
    static bool sprite_property(std::string_view property) {
        return property == "frame" || property == "width" || property == "height" ||
               property == "pivot_x" || property == "pivot_y" || property == "flip_x" ||
               property == "flip_y" || property == "tint_r" || property == "tint_g" ||
               property == "tint_b" || property == "tint_a" || property == "order" ||
               property == "image_mode" || property == "pixels_per_meter" ||
               property == "border_left" || property == "border_right" ||
               property == "border_top" || property == "border_bottom";
    }
    static bool material_property(std::string_view property) {
        return property == "material_parameters";
    }
    static bool particle_property(std::string_view property) {
        return property == "size_multiplier" || property == "tint_r" || property == "tint_g" ||
               property == "tint_b";
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
        const std::size_t value_limit =
            material_type(result.type) && material_property(result.property) ? 64 * 1024 : 8192;
        if (result.type.empty() || result.type.size() > 128 || result.property.empty() ||
            result.property.size() > 128 || request.at("value").dump().size() > value_limit)
            throw std::runtime_error("Live property target/value exceeds limits");
        if (result.target.scene != scene)
            throw std::runtime_error("Live property targets a different scene asset");
        return result;
    }
};
} // namespace forge
