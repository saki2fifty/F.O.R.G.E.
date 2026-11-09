#pragma once
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace forge {
// Transient byte ranges in one validated source snapshot; never DOM or asset IDs.
struct UiSourceRange {
    std::size_t begin = 0, end = 0;
    bool operator==(const UiSourceRange&) const = default;
};
struct UiMarkupAttribute {
    std::string name;
    UiSourceRange source, value;
    char quote = 0;
    bool has_value = false;
};
struct UiMarkupElement {
    static constexpr auto no_parent = std::numeric_limits<std::size_t>::max();
    std::string tag;
    std::size_t parent = no_parent;
    UiSourceRange source, opening, content, closing;
    std::vector<UiMarkupAttribute> attributes;
    bool self_closing = false;
};
struct UiStyleDeclaration {
    std::string name;
    UiSourceRange value;
};
// Uses the runtime resource validator's grammar and bounds. No DOM serialization.
std::vector<UiMarkupElement> inspect_ui_markup(std::string_view);
// Inline declarations only. Ranges retain the original bytes and ignore quoted
// semicolons, comments and function arguments using the same admission scanner.
std::vector<UiStyleDeclaration> inspect_ui_style(std::string_view);
} // namespace forge
