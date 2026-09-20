#include "reflected_extensions.hpp"
namespace forge::detail {
namespace {
using Json = nlohmann::json;
const Json empty;
const Json& child(const Json& object, const std::string& key) {
    return object.is_object() && object.contains(key) ? object.at(key) : empty;
}
Json extract(const Json& schema, const Json& value, unsigned depth) {
    if (depth > 8)
        throw std::runtime_error("Reflected extension nesting exceeds profile");
    const auto kind = schema.at("type").get<std::string>();
    if (kind == "struct") {
        Json result = value;
        for (const auto& field : schema.at("fields")) {
            const auto key = field.at("id").get<std::string>();
            auto part = extract(field, value.at(key), depth + 1);
            if (part.is_null())
                result.erase(key);
            else
                result[key] = std::move(part);
        }
        return result.empty() ? Json() : result;
    }
    if (kind == "array" || kind == "vector") {
        if (value.size() > 4096)
            throw std::runtime_error("Reflected extension collection exceeds profile");
        const auto key = schema.value("element_key", std::string{});
        Json result = key.empty() ? Json::array() : Json::object();
        bool any = false;
        for (const auto& element : value) {
            auto part = extract(schema.at("element"), element, depth + 1);
            any |= !part.is_null();
            if (key.empty())
                result.push_back(std::move(part));
            else if (!part.is_null())
                result[element.at(key).get<std::string>()] = std::move(part);
        }
        return any ? result : Json();
    }
    return nullptr;
}
Json merge(const Json& schema, const Json& known, const Json& extensions, unsigned depth) {
    if (depth > 8)
        throw std::runtime_error("Reflected extension nesting exceeds profile");
    const auto kind = schema.at("type").get<std::string>();
    if (kind == "struct") {
        Json result = extensions.is_object() ? extensions : Json::object();
        for (const auto& field : schema.at("fields")) {
            const auto key = field.at("id").get<std::string>();
            result[key] = merge(field, known.at(key), child(extensions, key), depth + 1);
        }
        return result;
    }
    if (kind == "array" || kind == "vector") {
        if (known.size() > 4096)
            throw std::runtime_error("Reflected extension collection exceeds profile");
        const auto key = schema.value("element_key", std::string{});
        Json result = Json::array();
        for (std::size_t i = 0; i < known.size(); ++i) {
            const auto& fragment = !key.empty()
                                       ? child(extensions, known[i].at(key).get<std::string>())
                                   : extensions.is_array() && i < extensions.size() ? extensions[i]
                                                                                    : empty;
            result.push_back(merge(schema.at("element"), known[i], fragment, depth + 1));
        }
        return result;
    }
    return known;
}
} // namespace
nlohmann::json reflected_extensions(const nlohmann::json& schema, const nlohmann::json& value) {
    return extract(schema, value, 0);
}
nlohmann::json merge_reflected_extensions(const nlohmann::json& schema, const nlohmann::json& known,
                                          const nlohmann::json& extensions) {
    return merge(schema, known, extensions, 0);
}
} // namespace forge::detail
