#pragma once
#include <forge/render_components.hpp>
#include <set>
#include <span>
namespace forge::detail {
inline void validate_material_slot_key(std::string_view key) {
    if (key.empty() || key.size() > 255)
        throw std::runtime_error("Invalid mesh material slot key length");
    for (const unsigned char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ':'))
            throw std::runtime_error("Invalid mesh material slot key character");
}
inline void validate_material_slots(std::span<const MaterialSlotOverride> values) {
    if (values.size() > 4096)
        throw std::runtime_error("Too many authored material slot overrides");
    std::set<std::string> keys;
    for (const auto& value : values) {
        validate_material_slot_key(value.slot);
        if (!keys.insert(value.slot).second)
            throw std::runtime_error("Duplicate authored material slot override");
    }
}
} // namespace forge::detail
