#include "gltf_scene.hpp"
#include "model_scene_values.hpp"
#include <algorithm>
namespace forge::asset_detail {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
const Json& array(const Json& row, const char* key) {
    static const auto empty = Json::array();
    if (!row.contains(key))
        return empty;
    const auto& result = row.at(key);
    require(result.is_array() && result.size() <= 100000,
            "Model scene metadata array exceeds bounds");
    return result;
}
const Json* extension(const GltfSourceBundle& source, const Json& row, const char* key) {
    if (!row.contains("extensions"))
        return nullptr;
    const auto& extensions = row.at("extensions");
    require(extensions.is_object(), "Invalid scene metadata extensions");
    if (!extensions.contains(key))
        return nullptr;
    const auto& used = array(source.document, "extensionsUsed");
    require(std::find(used.begin(), used.end(), key) != used.end(),
            "Scene extension is not declared in extensionsUsed");
    const auto& value = extensions.at(key);
    require(value.is_object(), "Scene extension value must be an object");
    return &value;
}
} // namespace
GltfSceneMetadata gltf_scene_metadata(const GltfSourceBundle& source) {
    GltfSceneMetadata result{Json::array(), Json::array(), Json::array()};
    for (const auto& camera : array(source.document, "cameras"))
        result.cameras.push_back(model_camera_value(camera));
    if (const auto* ext = extension(source, source.document, "KHR_lights_punctual")) {
        const auto& lights = array(*ext, "lights");
        require(!lights.empty(), "Punctual lights extension requires lights");
        for (const auto& light : lights)
            result.lights.push_back(model_light_value(light));
    }
    for (const auto& node : array(source.document, "nodes")) {
        Json values{{"light", nullptr}, {"visible", true}, {"selectable", true}};
        if (const auto* ext = extension(source, node, "KHR_lights_punctual")) {
            const auto& light = ext->at("light");
            require(light.is_number_integer() &&
                        (light.is_number_unsigned() || light.get<std::int64_t>() >= 0) &&
                        light.get<std::uint64_t>() < result.lights.size(),
                    "Node light index is invalid");
            values["light"] = light;
        }
        for (const auto& [key, property] : {std::pair{"KHR_node_visibility", "visible"},
                                            std::pair{"KHR_node_selectability", "selectable"}})
            if (const auto* ext = extension(source, node, key)) {
                const auto flag = ext->value(property, Json(true));
                require(flag.is_boolean(), "Node visibility/selectability must be boolean");
                values[property] = flag;
            }
        result.nodes.push_back(std::move(values));
    }
    return result;
}
} // namespace forge::asset_detail
