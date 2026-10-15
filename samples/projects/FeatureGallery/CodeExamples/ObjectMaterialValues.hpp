#pragma once
#include <algorithm>
#include <cmath>
#include <flecs.h>
#include <forge/render_components.hpp>
// Ordinary C++ changes an object's sparse numeric intent. Material resources,
// textures and shader programs stay shared. The renderer checks each value
// against the selected material and retains the last good draw on rejection.
inline bool forge_set_object_material_value(flecs::entity object, std::string_view slot,
                                            std::string_view key,
                                            forge::MaterialParameter parameter) {
    unsigned width;
    try {
        width = forge::material_parameter_width(parameter.type);
    } catch (const std::exception&) {
        return false;
    }
    const auto valid_token = [](std::string_view text, std::size_t limit, bool colon) {
        return !text.empty() && text.size() <= limit &&
               std::all_of(text.begin(), text.end(), [colon](unsigned char c) {
                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-' ||
                          (colon && c == ':');
               });
    };
    if (!object.has<forge::MeshRenderer>() || !valid_token(slot, 255, true) ||
        !valid_token(key, 256, false) || width == 0)
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!std::isfinite(parameter.value[i]) || (i >= width && parameter.value[i] != 0))
            return false;
    auto renderer = object.get<forge::MeshRenderer>();
    const auto row =
        std::find_if(renderer.material_parameters.begin(), renderer.material_parameters.end(),
                     [&](const auto& value) { return value.slot == slot && value.key == key; });
    if (row == renderer.material_parameters.end()) {
        if (renderer.material_parameters.size() >= forge::material_parameter_override_limit)
            return false;
        renderer.material_parameters.push_back({std::string(slot), std::string(key), parameter});
    } else
        row->parameter = parameter;
    object.set(renderer);
    return true;
}
// Removing intent follows the material again, including future material edits.
inline bool forge_revert_object_material_value(flecs::entity object, std::string_view slot,
                                               std::string_view key) {
    if (!object.has<forge::MeshRenderer>())
        return false;
    auto renderer = object.get<forge::MeshRenderer>();
    std::erase_if(renderer.material_parameters,
                  [&](const auto& value) { return value.slot == slot && value.key == key; });
    object.set(renderer);
    return true;
}
