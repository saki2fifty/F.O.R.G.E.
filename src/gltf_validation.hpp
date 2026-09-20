#pragma once
#include <limits>
#include <nlohmann/json.hpp>

namespace forge::gltf_detail {
using Json = nlohmann::json;
inline constexpr const char* meshopt_extension = "EXT_meshopt_compression";
inline const Json* extension(const Json& object, const char* name) {
    if (!object.contains("extensions"))
        return nullptr;
    const auto& extensions = object.at("extensions");
    if (!extensions.is_object())
        throw std::runtime_error("glTF extensions must be an object");
    const auto found = extensions.find(name);
    if (found == extensions.end())
        return nullptr;
    if (!found->is_object())
        throw std::runtime_error("glTF extension payload must be an object");
    return &*found;
}
inline std::size_t size_value(const Json& value) {
    if ((!value.is_number_unsigned() && !value.is_number_integer()) ||
        (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw std::runtime_error("glTF size/index must be a nonnegative integer");
    const auto number = value.get<std::uint64_t>();
    if (number > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("glTF size/index exceeds host limit");
    return static_cast<std::size_t>(number);
}
inline std::size_t size_or(const Json& object, const char* field, std::size_t fallback) {
    return object.contains(field) ? size_value(object.at(field)) : fallback;
}
inline const Json& array(const Json& document, const char* field, std::size_t limit) {
    static const Json empty = Json::array();
    if (!document.contains(field))
        return empty;
    const auto& value = document.at(field);
    if (!value.is_array() || value.size() > limit)
        throw std::runtime_error(std::string("glTF array exceeds limit or has wrong type: ") +
                                 field);
    return value;
}
} // namespace forge::gltf_detail
