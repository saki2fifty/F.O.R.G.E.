#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
struct UiValue {
    std::string name;
    double value = 0;
    bool operator==(const UiValue&) const = default;
};
// Scene-owned game values; RML retains the expressions that consume them.
// Native DOM handles, property paths and animation clocks are not stored here.
struct UiValues {
    static constexpr std::size_t max_entries = 16;
    std::vector<UiValue> values;
    static bool valid_name(std::string_view name) {
        constexpr std::string_view letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        constexpr std::string_view characters =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
        if (name.empty() || name.size() > 64 || letters.find(name.front()) == letters.npos ||
            name.find_first_not_of(characters) != name.npos)
            return false;
        std::string folded(name);
        std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) {
            return char(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        });
        constexpr std::array<std::string_view, 7> expression_words{
            "ev", "false", "it", "it_index", "literal", "size", "true"};
        return std::find(expression_words.begin(), expression_words.end(), folded) ==
                   expression_words.end() &&
               name != "tick" && name != "paused" && !name.starts_with("forge_loading_");
    }
    void validate() const {
        if (values.size() > max_entries)
            throw std::runtime_error("UI Values supports at most 16 named numbers");
        std::set<std::string> names;
        for (const auto& item : values) {
            if (!valid_name(item.name) || !names.insert(item.name).second)
                throw std::runtime_error("UI Values requires unique, nonreserved model names");
            if (!std::isfinite(item.value))
                throw std::runtime_error("UI Values requires finite numbers");
        }
    }
    bool operator==(const UiValues&) const = default;
};
} // namespace forge
