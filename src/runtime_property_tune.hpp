#pragma once
#include <forge/identity.hpp>
#include <forge/sprite_frames.hpp>
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
        return type == "forge.navigation_avoidance" || type == "forge.navigation_agent";
    }
    static bool navigation_property(std::string_view type, std::string_view property) {
        if (type == "forge.navigation_avoidance")
            return property == "enabled" || property == "horizon";
        if (type == "forge.navigation_agent")
            return property == "enabled" || property == "has_destination" || property == "speed" ||
                   property == "stopping_distance" || property == "destination_x" ||
                   property == "destination_y" || property == "destination_z";
        return false;
    }
    static bool particle_type(std::string_view type) { return type == "forge.particle_emitter"; }
    static bool material_type(std::string_view type) { return type == "forge.mesh_renderer"; }
    static bool probe_type(std::string_view type) { return type == "forge.reflection_probe"; }
    static bool decal_type(std::string_view type) { return type == "forge.decal"; }
    static bool decal_property(std::string_view property) {
        return property == "enabled" || property == "width" || property == "height" ||
               property == "depth" || property == "tint_r" || property == "tint_g" ||
               property == "tint_b" || property == "opacity" || property == "angle_fade_start" ||
               property == "angle_fade_end" || property == "layers" || property == "priority";
    }
    static bool light_type(std::string_view type) { return type == "forge.light"; }
    static bool light_property(std::string_view property) {
        return property == "shadow_source_radius" || property == "shadow_angular_radius" ||
               property == "emitter_radius" || property == "emitter_width" ||
               property == "emitter_height";
    }
    static bool probe_property(std::string_view property) {
        return property == "enabled" || property == "intensity" || property == "half_extent_x" ||
               property == "half_extent_y" || property == "half_extent_z" ||
               property == "blend_distance" || property == "rotation_degrees" ||
               property == "priority" || property == "box_projection";
    }
    static bool sprite_type(std::string_view type) { return type == "forge.sprite_renderer"; }
    static bool sprite_property(std::string_view property) {
        return property == "frame" || property == "width" || property == "height" ||
               property == "pivot_x" || property == "pivot_y" || property == "flip_x" ||
               property == "flip_y" || property == "tint_r" || property == "tint_g" ||
               property == "tint_b" || property == "tint_a" || property == "order" ||
               property == "image_mode" || property == "pixels_per_meter" ||
               property == "border_left" || property == "border_right" ||
               property == "border_top" || property == "border_bottom" ||
               property == "region_enabled" || property == "region_x" || property == "region_y" ||
               property == "region_width" || property == "region_height" ||
               property == "frame_regions" || property == "mip_filter";
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
        const bool collection =
            (material_type(result.type) && material_property(result.property)) ||
            (sprite_type(result.type) && result.property == "frame_regions");
        const std::size_t value_limit =
            sprite_type(result.type) && result.property == "frame_regions"
                ? sprite_frame_value_byte_limit
            : collection ? 64 * 1024
                         : 8192;
        if (result.type.empty() || result.type.size() > 128 || result.property.empty() ||
            result.property.size() > 128 || request.at("value").dump().size() > value_limit)
            throw std::runtime_error("Live property target/value exceeds limits");
        if (result.target.scene != scene)
            throw std::runtime_error("Live property targets a different scene asset");
        return result;
    }
};
} // namespace forge
