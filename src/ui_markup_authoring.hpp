#pragma once
#include "ui_markup.hpp"
#include <array>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace forge {
bool ui_markup_widget_editable(const UiMarkupElement&) noexcept;
// Optional literal adapters for authored controls, not computed CSS or admission.
enum class UiStyleUnit { Number, Pixels, DensityPixels, Percent };
inline constexpr std::array<std::string_view, 4> ui_style_unit_suffixes{"", "px", "dp", "%"};
struct UiStyleNumber {
    double value = 0;
    UiStyleUnit unit = UiStyleUnit::Number;
    bool operator==(const UiStyleNumber&) const = default;
};
std::optional<UiStyleNumber> read_ui_style_number(std::string_view);
std::string format_ui_style_number(UiStyleNumber);
inline constexpr std::array<std::string_view, 53> ui_common_style_properties{"color",
                                                                             "background-color",
                                                                             "opacity",
                                                                             "font-size",
                                                                             "font-family",
                                                                             "width",
                                                                             "height",
                                                                             "padding",
                                                                             "margin",
                                                                             "left",
                                                                             "top",
                                                                             "display",
                                                                             "text-align",
                                                                             "font-weight",
                                                                             "font-style",
                                                                             "text-decoration",
                                                                             "text-transform",
                                                                             "white-space",
                                                                             "line-height",
                                                                             "letter-spacing",
                                                                             "image-color",
                                                                             "flex-direction",
                                                                             "flex-wrap",
                                                                             "justify-content",
                                                                             "align-content",
                                                                             "align-items",
                                                                             "align-self",
                                                                             "flex-grow",
                                                                             "flex-shrink",
                                                                             "flex-basis",
                                                                             "row-gap",
                                                                             "column-gap",
                                                                             "min-width",
                                                                             "max-width",
                                                                             "min-height",
                                                                             "max-height",
                                                                             "position",
                                                                             "box-sizing",
                                                                             "overflow-x",
                                                                             "overflow-y",
                                                                             "padding-top",
                                                                             "padding-right",
                                                                             "padding-bottom",
                                                                             "padding-left",
                                                                             "margin-top",
                                                                             "margin-right",
                                                                             "margin-bottom",
                                                                             "margin-left",
                                                                             "border-width",
                                                                             "border-color",
                                                                             "border-radius",
                                                                             "right",
                                                                             "bottom"};
// Optional literal input controls, not a native CSS admission/cascade contract.
// Spans borrow immutable metadata; reading a profile allocates no storage.
struct UiStyleLiteralProfile {
    std::span<const std::string_view> keywords;
    std::span<const UiStyleUnit> units;
    bool color = false, integer = false;
    double minimum = -65536, maximum = 65536;
    UiStyleNumber initial;
};
std::optional<UiStyleLiteralProfile> ui_style_literal_profile(std::string_view property);
std::optional<UiStyleNumber> read_ui_style_literal_number(std::string_view property,
                                                          std::string_view literal);
std::string format_ui_style_literal_number(std::string_view property, UiStyleNumber);
using UiStyleColor = std::array<float, 4>;
std::optional<UiStyleColor> read_ui_style_color(std::string_view);
std::string format_ui_style_color(UiStyleColor);
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
    UiPixelLayout bounds; // Observed or expected native border box in common preview coordinates.
};
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 13>
    ui_layout_group_actions{{{"move", "Move group"},
                             {"align_left", "Align left"},
                             {"align_horizontal_center", "Align horizontal centers"},
                             {"align_right", "Align right"},
                             {"align_top", "Align top"},
                             {"align_vertical_center", "Align vertical centers"},
                             {"align_bottom", "Align bottom"},
                             {"distribute_horizontal", "Distribute horizontal gaps"},
                             {"distribute_vertical", "Distribute vertical gaps"},
                             {"match_width", "Match active width"},
                             {"match_height", "Match active height"},
                             {"match_size", "Match active size"},
                             {"snap_grid", "Snap positions to grid"}}};
// The document owner supplies admitted same-parent source/native observations.
std::vector<UiMarkupLayoutEdit> arrange_ui_markup_layouts(std::span<const UiMarkupLayoutBox>,
                                                          std::string_view action, float dx = 0,
                                                          float dy = 0, float grid = 8);
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 4>
    ui_layout_transform_actions{{{"move", "Move"},
                                 {"move_axis", "Constrain movement axis"},
                                 {"resize", "Resize"},
                                 {"resize_aspect", "Keep visible aspect"}}};
// Movement preserves group spacing; resize admits one member. Source bounds clamp
// like a canvas drag. Optional grid snaps the active border anchor/dominant size.
std::vector<UiMarkupLayoutBox> transform_ui_markup_layouts(std::span<const UiMarkupLayoutBox>,
                                                           std::string_view action, float dx,
                                                           float dy,
                                                           std::optional<float> grid = {});
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
// Same-parent editable siblings, input order retained in copied read values.
std::vector<std::optional<std::string>> read_ui_markup_styles(std::string_view,
                                                              std::span<const UiMarkupElement>,
                                                              std::span<const std::size_t>,
                                                              std::string_view property);
std::string set_ui_markup_styles(std::string_view, std::span<const std::size_t>,
                                 std::string_view property, std::optional<std::string> value);
std::vector<std::optional<std::string>> read_ui_markup_classes(std::string_view,
                                                               std::span<const UiMarkupElement>,
                                                               std::span<const std::size_t>);
std::string set_ui_markup_classes(std::string_view, std::span<const std::size_t>,
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
std::string duplicate_ui_stylesheet_rule(std::string_view, std::size_t rule);
std::string move_ui_stylesheet_rule(std::string_view, std::size_t rule, int direction);
std::string set_ui_markup_text(std::string_view, std::size_t element, std::string_view literal);
std::string insert_ui_markup_child(std::string_view, std::size_t parent, std::string_view tag,
                                   std::string_view literal = {},
                                   std::optional<std::size_t> before = {});
struct UiMarkupControlKind {
    std::string_view id, label, tag, input_type;
    bool accepts_text() const noexcept { return tag != "img" && tag != "br" && tag != "progress"; }
};
inline constexpr std::array<UiMarkupControlKind, 18> ui_markup_control_kinds{{
    {"container", "Container", "div", ""},
    {"inline-text", "Inline text", "span", ""},
    {"paragraph", "Paragraph", "p", ""},
    {"button", "Button", "button", ""},
    {"input-button", "Input button", "input", "button"},
    {"range", "Slider", "input", "range"},
    {"label", "Label", "label", ""},
    {"text-input", "Text input", "input", "text"},
    {"password-input", "Password input", "input", "password"},
    {"checkbox", "Checkbox", "input", "checkbox"},
    {"radio", "Radio option", "input", "radio"},
    {"textarea", "Text area", "textarea", ""},
    {"select", "Dropdown", "select", ""},
    {"option", "Dropdown option", "option", ""},
    {"progress", "Progress", "progress", ""},
    {"image", "Image", "img", ""},
    {"line-break", "Line break", "br", ""},
    {"form", "Form", "form", ""},
}};
struct UiMarkupControlEdit {
    std::string source;
    std::size_t element;
};
enum class UiControlPropertyType { Text, Flag, Number, Choice };
using UiControlPropertyValue = std::variant<std::monostate, bool, double, std::string>;
struct UiMarkupControlProperty {
    std::string name, label;
    UiControlPropertyType type = UiControlPropertyType::Text;
    std::vector<std::string> choices;
    UiControlPropertyValue value;
    std::optional<double> minimum, maximum, minimum_positive;
    bool integer = false;
    bool authored = false, available = false, editable = false;
    std::string reason;
};
struct UiMarkupControlProperties {
    std::string kind, label, reason;
    std::vector<UiMarkupControlProperty> properties;
};
std::vector<std::string> ui_markup_control_property_names();
// Authored literals/defaults, never computed or live native widget state.
// Borrow the outline from inspect_ui_markup of this exact source snapshot.
UiMarkupControlProperties read_ui_markup_control_properties(std::string_view,
                                                            std::span<const UiMarkupElement>,
                                                            std::size_t element);
// Null removes an authored property. Compose privately; publish once through the draft owner.
std::string
set_ui_markup_control_properties(std::string_view, std::size_t element,
                                 const std::map<std::string, UiControlPropertyValue>& properties);
// Compose existing source candidates; the caller publishes one transaction.
UiMarkupControlEdit insert_ui_markup_control(std::string_view, std::size_t parent,
                                             std::string_view kind, std::string_view literal = {},
                                             std::optional<std::size_t> before = {},
                                             std::string_view id = {},
                                             std::optional<UiPixelLayout> pixels = {});
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
