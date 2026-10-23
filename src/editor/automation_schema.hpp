#pragma once
#include <algorithm>
#include <forge/live_authoring.hpp>
namespace forge {
inline Json automation_object(Json properties = Json::object(), Json required = Json::array()) {
    return {{"type", "object"},
            {"properties", std::move(properties)},
            {"required", std::move(required)},
            {"additionalProperties", false}};
}
inline Json automation_text(std::size_t maximum = 1024, std::size_t minimum = 0) {
    return {{"type", "string"}, {"minLength", minimum}, {"maxLength", maximum}};
}
inline Json automation_integer(std::uint64_t minimum = 0,
                               std::uint64_t maximum = 9007199254740991ULL) {
    return {{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
}
inline Json automation_array(Json item, std::size_t maximum, std::size_t minimum = 0) {
    return {{"type", "array"},
            {"items", std::move(item)},
            {"minItems", minimum},
            {"maxItems", maximum}};
}
inline Json automation_uuid() { return automation_text(36, 36); }
inline Json automation_digest() { return automation_text(64, 64); }
inline std::filesystem::path automation_relative_path(const std::string& text) {
    const auto path = std::filesystem::u8path(text);
    if (text.empty() || text.size() > 2047 || path.is_absolute() || path.has_root_name() ||
        text.find_first_of("\\:") != std::string::npos ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; }) ||
        std::find(path.begin(), path.end(), "..") != path.end())
        throw AuthoringError("invalid_path", "Use a relative path with forward slashes");
    return path;
}
inline LiveAuthoringMethod automation_method(std::string name, std::string description,
                                             std::string capability, Json schema, bool mutation,
                                             std::function<Json(const Json&)> invoke,
                                             bool pending_control = false) {
    return {std::move(name), std::move(description), std::move(capability), std::move(schema),
            mutation,        std::move(invoke),      pending_control,       "operation_failed"};
}
} // namespace forge
