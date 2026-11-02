#pragma once
#include <algorithm>
#include <stdexcept>
#include <string_view>
namespace forge::navigation_detail {
inline void validate_profile_name(std::string_view profile) {
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (profile.empty() || profile.size() > 64 || !letter(profile.front()) ||
        !std::all_of(profile.begin(), profile.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }))
        throw std::runtime_error("Navigation profile must be 1-64 ASCII letters, digits, '_' or "
                                 "'-', beginning with a letter; names are case-sensitive");
}
} // namespace forge::navigation_detail
