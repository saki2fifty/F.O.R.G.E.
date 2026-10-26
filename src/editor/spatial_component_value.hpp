#pragma once
#include "../builtins.hpp"
#include <stdexcept>
#include <string_view>
namespace forge::ui {
// Detached spatial guides decode through the same native builtin validation.
template <class T> T spatial_component_value(const Json& values, const char* name) {
    detail::validate_components(Json{{name, values.at(name)}});
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == name)
            return std::get<T>(type.decode(values.at(name)));
    throw std::runtime_error("Unknown spatial helper component");
}
} // namespace forge::ui
