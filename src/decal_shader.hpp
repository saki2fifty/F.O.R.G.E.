#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
namespace forge {
inline constexpr unsigned decal_draw_limit = 8;
// The portable receiver profile reserves the existing lighting and comparison
// samplers. Full old materials keep their own requirements and receive no new
// bindings; extra samplers must never invalidate an otherwise admitted program.
inline unsigned decal_shader_capacity(std::size_t material_samplers, bool lit) {
    constexpr unsigned stage_budget = 16, lighting_samplers = 2;
    return lit && material_samplers < stage_budget - lighting_samplers
               ? unsigned(std::min<std::size_t>(decal_draw_limit, stage_budget - lighting_samplers -
                                                                      material_samplers))
               : 0;
}
inline constexpr unsigned decal_shader_rows(unsigned capacity, bool surface_channels = false) {
    return 1 + (surface_channels ? 13 : 8) * capacity;
}
inline std::string decal_shader_map_name(unsigned index, bool surface_channels) {
    return std::string(surface_channels ? "g_ForgeDecalMap" : "g_ForgeDecalColor") +
           std::to_string(index);
}
} // namespace forge
