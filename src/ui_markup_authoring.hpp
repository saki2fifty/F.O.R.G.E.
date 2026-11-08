#pragma once
#include "ui_markup.hpp"
#include <optional>

namespace forge {
bool ui_markup_widget_editable(const UiMarkupElement&) noexcept;
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
