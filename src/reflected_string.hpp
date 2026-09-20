#pragma once
#include <flecs.h>
#include <string>
namespace forge::detail {
// Explicit engine-owned std::string adapter, following pinned ser_std_vector.
// Registration alone never admits arbitrary opaque types into authoring.
inline flecs::opaque<std::string> reflected_string(flecs::world&) {
    return flecs::opaque<std::string>()
        .as_type(flecs::String)
        .serialize([](const flecs::serializer* serializer, const std::string* value) {
            if (value->size() >= 65536 || value->find('\0') != std::string::npos)
                return -1;
            const char* text = value->c_str();
            return serializer->value(flecs::String, &text);
        })
        .assign_string([](std::string* value, const char* text) { *value = text; });
}
} // namespace forge::detail
