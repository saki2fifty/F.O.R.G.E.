#pragma once
#include "ui_markup.hpp"
#include <optional>
#include <span>

namespace forge {
bool ui_markup_widget_editable(const UiMarkupElement&) noexcept;
struct UiPixelLayout {
    float left = 0, top = 0, width = 1, height = 1;
    bool operator==(const UiPixelLayout&) const = default;
};
// Bounded plain inline absolute-pixel subset. Native preview eligibility is an
// additional guard supplied by the document owner, never inferred from source.
UiPixelLayout read_ui_markup_layout(std::string_view, std::size_t element);
// Borrow ranges from inspect_ui_markup of this exact source snapshot. Read-only
// outline consumers avoid reparsing the document for each observed element.
UiPixelLayout read_ui_markup_layout(std::string_view, std::span<const UiMarkupElement>,
                                    std::size_t element);
std::string set_ui_markup_layout(std::string_view, std::size_t element, UiPixelLayout);
// Detached source candidates only. The owning document supplies revision/history.
std::string set_ui_markup_attribute(std::string_view, std::size_t element, std::string_view name,
                                    std::optional<std::string> value);
std::string set_ui_markup_text(std::string_view, std::size_t element, std::string_view literal);
std::string insert_ui_markup_child(std::string_view, std::size_t parent, std::string_view tag,
                                   std::string_view literal = {},
                                   std::optional<std::size_t> before = {});
std::string remove_ui_markup_element(std::string_view, std::size_t element);
std::string move_ui_markup_element(std::string_view, std::size_t element, int direction);
} // namespace forge
