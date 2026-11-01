#pragma once
#include "builtins.hpp"
#include <string_view>
namespace forge::navigation_detail {
inline const detail::Builtin& navigation_descriptor(std::string_view name) {
    for (const auto& type : detail::builtins())
        if (std::string_view(type.name) == name)
            return type;
    throw std::logic_error("Navigation schema is missing");
}
} // namespace forge::navigation_detail
