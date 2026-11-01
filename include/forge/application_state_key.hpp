#pragma once
#include <algorithm>
#include <string_view>

namespace forge {
// Semantic identifiers shared by source validation, the host and exact SDK.
inline bool application_state_key(std::string_view key) noexcept {
    if (key.empty() || key.size() > 48 || key.front() < 'a' || key.front() > 'z')
        return false;
    return std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}
} // namespace forge
