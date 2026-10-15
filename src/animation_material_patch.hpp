#pragma once
#include <algorithm>
#include <forge/render_components.hpp>
#include <nlohmann/json.hpp>
#include <span>
namespace forge {
// Merge a copied presentation/preview only. Stable business keys retain unknown
// row/parameter extensions; native samples never replace the entire source row.
inline void merge_animation_material_parameters(nlohmann::json& renderer,
                                                const nlohmann::json& values) {
    using Json = nlohmann::json;
    if (!renderer.contains("material_parameters"))
        renderer["material_parameters"] = Json::array();
    auto& rows = renderer.at("material_parameters");
    for (const auto& value : values) {
        auto at = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
            return row.at("slot") == value.at("slot") && row.at("key") == value.at("key");
        });
        if (at == rows.end())
            rows.push_back(value);
        else {
            (*at)["parameter"]["type"] = value.at("parameter").at("type");
            (*at)["parameter"]["value"] = value.at("parameter").at("value");
        }
    }
}
inline nlohmann::json
animation_material_parameters_json(std::span<const MaterialParameterOverride> values) {
    auto result = nlohmann::json::array();
    for (const auto& value : values)
        result.push_back(
            {{"slot", value.slot},
             {"key", value.key},
             {"parameter",
              {{"type", int(value.parameter.type)}, {"value", value.parameter.value}}}});
    return result;
}
} // namespace forge
