#pragma once
#include "ui_markup.hpp"
#include <array>
#include <optional>
#include <span>
#include <utility>

namespace forge {
bool ui_markup_widget_editable(const UiMarkupElement&) noexcept;
struct UiPixelLayout {
    float left = 0, top = 0, width = 1, height = 1;
    bool operator==(const UiPixelLayout&) const = default;
};
inline constexpr std::size_t ui_source_selection_limit = 32;
struct UiMarkupLayoutEdit {
    std::size_t element;
    UiPixelLayout pixels;
    bool operator==(const UiMarkupLayoutEdit&) const = default;
};
struct UiMarkupLayoutBox {
    UiMarkupLayoutEdit source;
    UiPixelLayout bounds; // Borrowed native border box in the common preview coordinates.
};
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 9>
    ui_layout_group_actions{{{"move", "Move group"},
                             {"align_left", "Align left"},
                             {"align_horizontal_center", "Align horizontal centers"},
                             {"align_right", "Align right"},
                             {"align_top", "Align top"},
                             {"align_vertical_center", "Align vertical centers"},
                             {"align_bottom", "Align bottom"},
                             {"distribute_horizontal", "Distribute horizontal gaps"},
                             {"distribute_vertical", "Distribute vertical gaps"}}};
// The document owner supplies admitted same-parent source/native observations.
std::vector<UiMarkupLayoutEdit> arrange_ui_markup_layouts(std::span<const UiMarkupLayoutBox>,
                                                          std::string_view action, float dx = 0,
                                                          float dy = 0);
// Bounded plain inline absolute-pixel subset. Native preview eligibility is an
// additional guard supplied by the document owner, never inferred from source.
UiPixelLayout read_ui_markup_layout(std::string_view, std::size_t element);
// Borrow ranges from inspect_ui_markup of this exact source snapshot. Read-only
// outline consumers avoid reparsing the document for each observed element.
UiPixelLayout read_ui_markup_layout(std::string_view, std::span<const UiMarkupElement>,
                                    std::size_t element);
std::string set_ui_markup_layout(std::string_view, std::size_t element, UiPixelLayout);
// One admitted source snapshot, one complete candidate; no partial publication.
std::string set_ui_markup_layouts(std::string_view, std::span<const UiMarkupLayoutEdit>);
// Detached source candidates only. The owning document supplies revision/history.
std::string set_ui_markup_attribute(std::string_view, std::size_t element, std::string_view name,
                                    std::optional<std::string> value);
// Literal inline declarations only; null removes the matching property override.
std::optional<std::string> read_ui_markup_style(std::string_view, std::span<const UiMarkupElement>,
                                                std::size_t element, std::string_view property);
std::string set_ui_markup_style(std::string_view, std::size_t element, std::string_view property,
                                std::optional<std::string> value);
std::optional<std::string> read_ui_stylesheet_property(std::string_view,
                                                       std::span<const UiStyleRule>,
                                                       std::size_t rule, std::string_view property);
std::string set_ui_stylesheet_property(std::string_view, std::size_t rule,
                                       std::string_view property, std::optional<std::string> value);
std::string set_ui_stylesheet_selector(std::string_view, std::size_t rule,
                                       std::string_view selector);
std::string insert_ui_stylesheet_rule(std::string_view, std::string_view selector);
std::string remove_ui_stylesheet_rule(std::string_view, std::size_t rule);
std::string set_ui_markup_text(std::string_view, std::size_t element, std::string_view literal);
std::string insert_ui_markup_child(std::string_view, std::size_t parent, std::string_view tag,
                                   std::string_view literal = {},
                                   std::optional<std::size_t> before = {});
std::string remove_ui_markup_element(std::string_view, std::size_t element);
std::string remove_ui_markup_elements(std::string_view, std::span<const std::size_t> elements);
// Move static source siblings into a container, retaining source order and IDs.
std::string reparent_ui_markup_elements(std::string_view, std::span<const std::size_t> elements,
                                        std::size_t parent, std::optional<std::size_t> before = {});
// Copy a static subtree next to itself; empty prefix chooses an available copy prefix.
std::string duplicate_ui_markup_element(std::string_view, std::size_t element,
                                        std::string_view prefix = {});
std::string move_ui_markup_element(std::string_view, std::size_t element, int direction);
} // namespace forge
