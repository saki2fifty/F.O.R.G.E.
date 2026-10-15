#pragma once
#include <string>
#include <string_view>
namespace forge {
inline std::string material_field_label(std::string_view name) {
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z' && !out.empty())
            out += ' ';
        out += c;
    }
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z')
        out[0] -= 'a' - 'A';
    return out;
}
// Imported legacy/default values can include fields unused by their selected
// shader model. Preserve those values, but do not advertise ineffective controls.
inline bool material_parameter_visible(std::string_view model, std::string_view key) {
    if (model == "forge.gltf.unlit.v1")
        return key == "baseColorFactor";
    if (model == "forge.gltf.specular-glossiness.v1")
        return key != "ior" && key != "dispersion" && !key.starts_with("clearcoat") &&
               key != "metallicFactor";
    return true;
}
} // namespace forge
