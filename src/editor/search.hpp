#pragma once
#include <cctype>
#include <string>
namespace forge {
inline std::string search_key(std::string value) {
    for (auto& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
} // namespace forge
