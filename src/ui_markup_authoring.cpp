#include "ui_markup_authoring.hpp"
#include "ui_font_profile.hpp"
#include "ui_range_numeric.hpp"
#include <RmlUi/Core/Rectangle.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi/Core/Types.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <forge/project_paths.hpp>
#include <forge/ui_assets.hpp>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace forge {
bool ui_markup_widget_editable(const UiMarkupElement& node) noexcept {
    return node.tag != "rml" && node.tag != "head" && node.tag != "body" && node.tag != "style" &&
           node.tag != "title" && node.tag != "link" && node.tag != "meta";
}
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
const UiMarkupElement& element(std::span<const UiMarkupElement> nodes, std::size_t index) {
    require(index < nodes.size(), "Choose an element from the current UI source outline");
    return nodes[index];
}
void editable(const UiMarkupElement& node) {
    require(ui_markup_widget_editable(node),
            "Edit document structure, styles and resource declarations in the source view");
}
void identifier(std::string_view name) {
    require(!name.empty() && name.size() <= 64 && name.front() >= 'a' && name.front() <= 'z' &&
                std::ranges::all_of(name,
                                    [](char c) {
                                        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                               c == '-' || c == '_';
                                    }),
            "Use a lowercase UI name starting with a letter, at most64 letters/digits/_/-");
}
std::string escaped(std::string_view value) {
    require(value.size() <= 256 * 1024, "UI text exceeds 256 KiB");
    return Rml::StringUtilities::EncodeRml(std::string(value));
}
std::string replaced(std::string_view source, UiSourceRange range, std::string_view value,
                     bool markup = true) {
    std::string result(source);
    result.replace(range.begin, range.end - range.begin, value);
    validate_ui_text(result, markup);
    return result;
}
using SourceEdit = std::pair<UiSourceRange, std::string>;
std::string markup_edits(std::string_view source, std::vector<SourceEdit> edits) {
    if (edits.empty())
        return std::string(source);
    std::ranges::sort(edits,
                      [](const auto& a, const auto& b) { return a.first.begin > b.first.begin; });
    std::string result(source);
    auto next = source.size();
    for (const auto& [range, value] : edits) {
        require(range.begin <= range.end && range.end <= next, "Source replacements overlap");
        next = range.begin;
        result.replace(range.begin, range.end - range.begin, value);
    }
    validate_ui_text(result, true);
    return result;
}
std::size_t attribute_insertion(std::string_view source, const UiMarkupElement& node) {
    auto position = node.opening.end - 1;
    if (source[position - 1] == '/')
        --position;
    return position;
}
const UiMarkupAttribute* markup_attribute(const UiMarkupElement& node, std::string_view name) {
    const UiMarkupAttribute* found = nullptr;
    for (const auto& attribute : node.attributes)
        if (attribute.name == name) {
            require(!found, "Resolve duplicate attributes in the source view first");
            found = &attribute;
        }
    return found;
}
SourceEdit attribute_edit(std::string_view source, const UiMarkupElement& node,
                          std::string_view name, const std::optional<std::string>& value) {
    require(node.tag != "head" && node.tag != "style" && node.tag != "link" && node.tag != "meta",
            "Edit resource and style attributes in the source view");
    const auto* found = markup_attribute(node, name);
    const auto insertion = attribute_insertion(source, node);
    if (!value)
        return {found ? found->source : UiSourceRange{insertion, insertion}, {}};
    const auto assignment = std::string(name) + "=\"" + escaped(*value) + '"';
    return found ? SourceEdit{found->source, assignment}
                 : SourceEdit{{insertion, insertion}, " " + assignment};
}
void literal_classes(std::string_view value) {
    require(!value.empty() && value.size() <= 1024,
            "Use1..1024 bytes of literal classes, or clear them");
    std::vector<std::string_view> names;
    for (auto begin = value.find_first_not_of(' '); begin != value.npos;) {
        auto end = value.find(' ', begin);
        if (end == value.npos)
            end = value.size();
        const auto name = value.substr(begin, end - begin);
        const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
        require(name.size() <= 64 && (letter(name.front()) || name.front() == '_') &&
                    std::ranges::all_of(name,
                                        [&](char c) {
                                            return letter(c) || (c >= '0' && c <= '9') ||
                                                   c == '_' || c == '-';
                                        }),
                "Use space-separated ASCII classes starting with a letter or underscore, up to64 "
                "bytes each");
        require(names.size() < 32 && std::ranges::find(names, name) == names.end(),
                "Use at most32 distinct class names");
        names.push_back(name);
        begin = value.find_first_not_of(' ', end);
    }
    require(!names.empty(), "Enter a literal class list or clear it");
}
std::optional<std::string> class_value(std::string_view source, const UiMarkupElement& node) {
    const auto* attribute = markup_attribute(node, "class");
    if (attribute) {
        require(attribute->has_value, "Class attributes without values use Source");
        const auto text =
            source.substr(attribute->value.begin, attribute->value.end - attribute->value.begin);
        auto value = Rml::StringUtilities::DecodeRml(std::string(text));
        require(value.find("{{") == value.npos && value.find('\0') == value.npos,
                "Dynamic or NUL-containing classes use Source");
        return value;
    }
    return {};
}
bool contains(UiSourceRange outer, UiSourceRange inner) {
    return inner.begin >= outer.begin && inner.end <= outer.end;
}
void static_attributes(const UiMarkupElement& node) {
    require(node.tag != "script" && node.tag != "template",
            "Edit script/template declarations explicitly in Source");
    for (const auto& attr : node.attributes)
        require(!attr.name.starts_with("data-") && !attr.name.starts_with("on"),
                "Edit bound or event-driven UI explicitly in Source");
}
void static_ancestry(std::span<const UiMarkupElement> nodes, std::size_t index) {
    for (auto ancestor = index; ancestor != UiMarkupElement::no_parent;
         ancestor = nodes[ancestor].parent)
        static_attributes(nodes[ancestor]);
}
void static_subtree(std::string_view source, std::span<const UiMarkupElement> nodes,
                    std::size_t index) {
    const auto& root = element(nodes, index);
    static_ancestry(nodes, index);
    const auto fragment = source.substr(root.source.begin, root.source.end - root.source.begin);
    require(fragment.find("{{") == fragment.npos, "Edit text expressions explicitly in Source");
    for (const auto& node : nodes)
        if (contains(root.source, node.source)) {
            editable(node);
            static_attributes(node);
            bool label_target = false;
            for (const auto& attr : node.attributes)
                if (node.tag == "label" && attr.name == "for") {
                    require(!label_target && attr.has_value,
                            "Use one literal target for each source label");
                    label_target = true;
                } else if (attr.name == "href" && attr.has_value) {
                    const auto href = Rml::StringUtilities::DecodeRml(std::string(
                        source.substr(attr.value.begin, attr.value.end - attr.value.begin)));
                    require(!href.starts_with('#'),
                            "Edit fragment references explicitly in Source");
                }
        }
}
std::map<std::string, UiSourceRange> literal_ids(std::string_view source,
                                                 std::span<const UiMarkupElement> nodes) {
    std::map<std::string, UiSourceRange> ids;
    for (const auto& node : nodes) {
        bool has_id = false;
        for (const auto& attr : node.attributes)
            if (attr.name == "id") {
                require(!has_id && attr.has_value, "Resolve ambiguous source IDs first");
                has_id = true;
                auto id = Rml::StringUtilities::DecodeRml(std::string(
                    source.substr(attr.value.begin, attr.value.end - attr.value.begin)));
                require(!id.empty() && id.size() <= 128 && id.find_first_of("{}") == id.npos &&
                            std::ranges::all_of(
                                id, [](unsigned char c) { return c >= 32 && c != 127; }),
                        "Use nonempty literal UI IDs of at most128 bytes");
                require(ids.emplace(std::move(id), attr.source).second,
                        "Resolve duplicate source IDs first");
            }
    }
    return ids;
}
std::vector<UiSourceRange> sibling_ranges(std::span<const UiMarkupElement> nodes,
                                          std::span<const std::size_t> indices) {
    require(!indices.empty() && indices.size() <= ui_source_selection_limit,
            "Choose1..32 source siblings");
    std::optional<std::size_t> parent;
    std::vector<UiSourceRange> ranges;
    ranges.reserve(indices.size());
    for (const auto index : indices) {
        const auto& node = element(nodes, index);
        editable(node);
        if (parent)
            require(*parent == node.parent, "Choose source siblings with the same parent");
        parent = node.parent;
        require(
            std::ranges::none_of(ranges, [&](const auto& range) { return range == node.source; }),
            "A source group cannot repeat an element");
        ranges.push_back(node.source);
    }
    std::ranges::sort(ranges, [](const auto& a, const auto& b) { return a.begin > b.begin; });
    return ranges;
}
std::string without_ranges(std::string_view source, std::span<const UiSourceRange> descending) {
    std::string result(source);
    std::size_t next = source.size();
    for (const auto range : descending) {
        require(range.begin <= range.end && range.end <= next, "Source ranges overlap");
        next = range.begin;
        result.erase(range.begin, range.end - range.begin);
    }
    return result;
}
struct LayoutSource {
    UiPixelLayout pixels;
    std::array<UiSourceRange, 4> values;
};
UiSourceRange trimmed(std::string_view source, UiSourceRange range) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (range.begin < range.end && space(source[range.begin]))
        ++range.begin;
    while (range.end > range.begin && space(source[range.end - 1]))
        --range.end;
    return range;
}
struct InlineStyleSource {
    const UiMarkupAttribute* attribute = nullptr;
    std::string_view text;
    std::vector<UiStyleDeclaration> declarations;
};
InlineStyleSource inline_style_source(std::string_view source, const UiMarkupElement& node) {
    InlineStyleSource result;
    for (const auto& attr : node.attributes)
        if (attr.name == "style") {
            require(!result.attribute && attr.has_value && attr.quote,
                    "Use one quoted inline style; edit other forms in Source");
            result.attribute = &attr;
        }
    if (result.attribute) {
        const auto range = result.attribute->value;
        result.text = source.substr(range.begin, range.end - range.begin);
        require(result.text.find('&') == result.text.npos,
                "Entity-encoded inline styles use Source");
        result.declarations = inspect_ui_style(result.text);
    }
    return result;
}
const UiStyleDeclaration* style_property(std::string_view text,
                                         std::span<const UiStyleDeclaration> declarations,
                                         std::string_view name) {
    const UiStyleDeclaration* found = nullptr;
    for (const auto& declaration : declarations)
        if (declaration.name == name) {
            require(!found, "Resolve duplicate matching properties in Source first");
            const auto range =
                trimmed(text, {declaration.source.begin, declaration.value.begin - 1});
            const auto literal = text.substr(range.begin, range.end - range.begin);
            require(std::ranges::equal(literal, declaration.name,
                                       [](char c, char expected) {
                                           if (c >= 'A' && c <= 'Z')
                                               c = char(c - 'A' + 'a');
                                           return c == expected;
                                       }),
                    "Use a plain property name; edit split/commented names in Source");
            found = &declaration;
        }
    return found;
}
std::pair<UiSourceRange, std::string>
style_value_edit(std::string_view text, std::span<const UiStyleDeclaration> declarations,
                 std::string_view property, const std::optional<std::string>& value) {
    const auto* found = style_property(text, declarations, property);
    if (!value)
        return {found ? found->source : UiSourceRange{text.size(), text.size()}, {}};
    require(value->size() <= 8192 && value->find_first_of("<>") == value->npos &&
                value->find("{{") == value->npos,
            "Use one literal CSS value without markup, up to8192 bytes");
    const auto nonspace = trimmed(*value, {0, value->size()});
    require(nonspace.begin != nonspace.end, "Enter a CSS value or remove the property");
    const auto declaration = std::string(property) + ':' + *value + ';';
    const auto checked = inspect_ui_style(declaration);
    require(checked.size() == 1 && checked[0].name == property &&
                checked[0].value.begin == property.size() + 1 &&
                checked[0].value.end == declaration.size() - 1,
            "Enter one CSS value without extra declarations");
    if (found)
        return {trimmed(text, found->value), *value};
    const bool separator =
        !declarations.empty() && declarations.back().source.end == declarations.back().value.end;
    return {{text.size(), text.size()}, (separator ? ";" : "") + declaration};
}
std::pair<UiSourceRange, std::string> style_edit(std::string_view text,
                                                 std::span<const UiStyleDeclaration> declarations,
                                                 std::string_view property,
                                                 const std::optional<std::string>& value) {
    identifier(property);
    return style_value_edit(text, declarations, property, value);
}
SourceEdit markup_style_edit(std::string_view source, const UiMarkupElement& node,
                             std::string_view property, const std::optional<std::string>& value) {
    const auto style = inline_style_source(source, node);
    require(!value || value->find('&') == value->npos, "Entity-encoded inline styles use Source");
    const auto edit = style_edit(style.text, style.declarations, property, value);
    if (!style.attribute) {
        const auto position = attribute_insertion(source, node);
        if (!value)
            return {{position, position}, {}};
        const auto& declaration = edit.second;
        const char quote = declaration.find('"') == declaration.npos ? '"' : '\'';
        require(declaration.find(quote) == declaration.npos,
                "Values requiring both attribute quote forms use Source");
        return {{position, position}, " style=" + std::string(1, quote) + declaration + quote};
    }
    require(!value || value->find(style.attribute->quote) == value->npos,
            "Use CSS quotes different from the inline attribute quotes, or edit in Source");
    const auto offset = style.attribute->value.begin;
    return {{offset + edit.first.begin, offset + edit.first.end}, edit.second};
}
const UiStyleRule& stylesheet_rule(std::span<const UiStyleRule> rules, std::size_t index) {
    require(index < rules.size(), "Choose a rule from the current stylesheet outline");
    const auto& rule = rules[index];
    require(rule.flat, "At-rules and nested stylesheet blocks use Source");
    return rule;
}
void stylesheet_selector(std::string_view selector) {
    require(!selector.empty() && selector.size() <= 1024 &&
                selector.find_first_of("@;<\\") == selector.npos &&
                selector.find("/*") == selector.npos && selector.find("{{") == selector.npos,
            "Use a literal selector up to1024 bytes; at-rules/comments/escapes use Source");
    const auto candidate = std::string(selector) + "{}";
    const auto rules = inspect_ui_stylesheet(candidate);
    require(rules.size() == 1 && rules.front().flat && rules.front().selector.begin == 0 &&
                rules.front().selector.end == selector.size(),
            "Enter one complete stylesheet selector without extra blocks");
}
void valid_layout(UiPixelLayout value) {
    for (const auto number : {value.left, value.top, value.width, value.height})
        require(std::isfinite(number) && std::abs(number) <= 65536,
                "Canvas layout values must be finite pixels within65536");
    require(value.width >= 1 && value.height >= 1,
            "Canvas width and height must be at least1 pixel");
}
LayoutSource layout_source(std::string_view source, std::span<const UiMarkupElement> nodes,
                           std::size_t index) {
    const auto& node = element(nodes, index);
    editable(node);
    for (auto parent = index; parent != UiMarkupElement::no_parent;
         parent = element(nodes, parent).parent)
        for (const auto& attr : element(nodes, parent).attributes)
            require(!attr.name.starts_with("data-"),
                    "Data-bound elements and ancestors use Source for layout edits");
    const auto style = inline_style_source(source, node);
    require(style.attribute, "Define the absolute pixel layout in the element's inline style");
    const auto inline_style = style.text;
    const auto value_range = [&](std::string_view name) {
        const auto* found = style_property(style.text, style.declarations, name);
        require(found, "Define inline position:absolute and pixel left, top, width and height");
        return trimmed(inline_style, found->value);
    };
    const auto position = value_range("position");
    require(inline_style.substr(position.begin, position.end - position.begin) == "absolute",
            "Canvas layout requires inline position:absolute");
    LayoutSource result;
    std::array<float*, 4> values{&result.pixels.left, &result.pixels.top, &result.pixels.width,
                                 &result.pixels.height};
    constexpr std::array names{"left", "top", "width", "height"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto range = value_range(names[i]);
        const auto value = inline_style.substr(range.begin, range.end - range.begin);
        require(value.size() > 2 && value.ends_with("px"),
                "Canvas layout requires plain inline pixel values");
        const auto number = value.substr(0, value.size() - 2);
        const auto parsed =
            std::from_chars(number.data(), number.data() + number.size(), *values[i]);
        require(parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size(),
                "Canvas layout requires plain inline pixel values");
        result.values[i] = {style.attribute->value.begin + range.begin,
                            style.attribute->value.begin + range.end};
    }
    valid_layout(result.pixels);
    return result;
}
} // namespace
std::optional<UiStyleNumber> read_ui_style_number(std::string_view text) {
    const auto range = trimmed(text, {0, text.size()});
    text = text.substr(range.begin, range.end - range.begin);
    for (std::size_t i = ui_style_unit_suffixes.size(); i-- > 0;) {
        const auto suffix = ui_style_unit_suffixes[i];
        if (!text.ends_with(suffix))
            continue;
        const auto number = text.substr(0, text.size() - suffix.size());
        if (number.empty())
            return {};
        double value = 0;
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size() &&
            std::isfinite(value) && std::abs(value) <= 65536)
            return UiStyleNumber{value, static_cast<UiStyleUnit>(i)};
        return {};
    }
    return {};
}
std::string format_ui_style_number(UiStyleNumber number) {
    const auto unit = static_cast<std::size_t>(number.unit);
    require(unit < ui_style_unit_suffixes.size() && std::isfinite(number.value) &&
                std::abs(number.value) <= 65536,
            "Use a finite style number between -65536 and 65536 and a supported unit");
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number.value);
    require(result.ec == std::errc{}, "Could not format the style number");
    return std::string(buffer.data(), result.ptr) + std::string(ui_style_unit_suffixes[unit]);
}
std::optional<UiStyleLiteralProfile> ui_style_literal_profile(std::string_view property) {
    static constexpr std::array all_units{UiStyleUnit::Number, UiStyleUnit::Pixels,
                                          UiStyleUnit::DensityPixels, UiStyleUnit::Percent};
    static constexpr std::array lengths{UiStyleUnit::Pixels, UiStyleUnit::DensityPixels};
    static constexpr std::array unitless{UiStyleUnit::Number};
    static constexpr std::array<std::string_view, 2> weights{"normal", "bold"};
    static constexpr std::array<std::string_view, 2> styles{"normal", "italic"};
    static constexpr std::array<std::string_view, 4> alignment{"left", "right", "center",
                                                               "justify"};
    static constexpr std::array<std::string_view, 4> decoration{"none", "underline", "overline",
                                                                "line-through"};
    static constexpr std::array<std::string_view, 4> transform{"none", "capitalize", "uppercase",
                                                               "lowercase"};
    static constexpr std::array<std::string_view, 5> whitespace{"normal", "pre", "nowrap",
                                                                "pre-wrap", "pre-line"};
    static constexpr std::array<std::string_view, 1> normal{"normal"};
    static constexpr std::array dimensions{UiStyleUnit::Pixels, UiStyleUnit::DensityPixels,
                                           UiStyleUnit::Percent};
    static constexpr std::array<std::string_view, 7> display{
        "none", "block", "inline", "inline-block", "flow-root", "flex", "inline-flex"};
    static constexpr std::array<std::string_view, 4> directions{"row", "row-reverse", "column",
                                                                "column-reverse"};
    static constexpr std::array<std::string_view, 3> wrapping{"nowrap", "wrap", "wrap-reverse"};
    static constexpr std::array<std::string_view, 7> content{
        "flex-start",   "flex-end",     "center", "space-between",
        "space-around", "space-evenly", "stretch"};
    static constexpr std::array<std::string_view, 6> items{"auto",   "flex-start", "flex-end",
                                                           "center", "baseline",   "stretch"};
    static constexpr std::array<std::string_view, 1> automatic{"auto"}, none{"none"};
    static constexpr std::array<std::string_view, 4> positioning{"static", "relative", "absolute",
                                                                 "fixed"};
    static constexpr std::array<std::string_view, 2> sizing{"content-box", "border-box"};
    static constexpr std::array<std::string_view, 4> overflow{"visible", "hidden", "auto",
                                                              "scroll"};
    UiStyleLiteralProfile profile;
    if (property == "color" || property == "background-color" || property == "image-color" ||
        property == "border-color")
        profile.color = true;
    else if (std::ranges::any_of(std::array{"opacity", "font-size", "width", "height", "padding",
                                            "margin", "left", "top", "right", "bottom"},
                                 [&](const auto* name) { return property == name; })) {
        profile.units = all_units;
    } else if (property == "font-weight") {
        profile.keywords = weights;
        profile.units = unitless;
        profile.minimum = 1;
        profile.maximum = 1000;
        profile.integer = true;
        profile.initial = {400, UiStyleUnit::Number};
    } else if (property == "font-style")
        profile.keywords = styles;
    else if (property == "text-align")
        profile.keywords = alignment;
    else if (property == "text-decoration")
        profile.keywords = decoration;
    else if (property == "text-transform")
        profile.keywords = transform;
    else if (property == "white-space")
        profile.keywords = whitespace;
    else if (property == "letter-spacing") {
        profile.keywords = normal;
        profile.units = lengths;
        profile.initial = {0, UiStyleUnit::Pixels};
    } else if (property == "line-height") {
        profile.units = all_units;
        profile.minimum = 0;
        profile.initial = {1.2, UiStyleUnit::Number};
    } else if (property == "display")
        profile.keywords = display;
    else if (property == "flex-direction")
        profile.keywords = directions;
    else if (property == "flex-wrap")
        profile.keywords = wrapping;
    else if (property == "justify-content")
        profile.keywords = std::span(content).first(6);
    else if (property == "align-content")
        profile.keywords = content;
    else if (property == "align-items")
        profile.keywords = std::span(items).subspan(1);
    else if (property == "align-self")
        profile.keywords = items;
    else if (property == "flex-grow" || property == "flex-shrink") {
        profile.units = unitless;
        profile.minimum = 0;
        profile.initial = {property == "flex-shrink" ? 1.0 : 0.0, UiStyleUnit::Number};
    } else if (property == "flex-basis" || property == "row-gap" || property == "column-gap" ||
               property == "min-width" || property == "max-width" || property == "min-height" ||
               property == "max-height") {
        profile.units = dimensions;
        profile.minimum = 0;
        profile.initial = {0, UiStyleUnit::Pixels};
        if (property == "flex-basis")
            profile.keywords = automatic;
        else if (property == "max-width" || property == "max-height")
            profile.keywords = none;
    } else if (property == "position")
        profile.keywords = positioning;
    else if (property == "box-sizing")
        profile.keywords = sizing;
    else if (property == "overflow-x" || property == "overflow-y")
        profile.keywords = overflow;
    else if (property == "border-width" || property == "border-radius") {
        profile.units = lengths;
        profile.minimum = 0;
        profile.initial = {0, UiStyleUnit::Pixels};
    } else if (property == "padding-top" || property == "padding-right" ||
               property == "padding-bottom" || property == "padding-left" ||
               property == "margin-top" || property == "margin-right" ||
               property == "margin-bottom" || property == "margin-left") {
        profile.units = dimensions;
        profile.initial = {0, UiStyleUnit::Pixels};
        if (property.starts_with("padding-"))
            profile.minimum = 0;
        else
            profile.keywords = automatic;
    } else if (property != "font-family")
        return {};
    return profile;
}
namespace {
bool style_number_in_profile(const UiStyleLiteralProfile& profile, UiStyleNumber number) {
    return std::isfinite(number.value) && number.value >= profile.minimum &&
           number.value <= profile.maximum &&
           (!profile.integer || std::trunc(number.value) == number.value) &&
           std::ranges::find(profile.units, number.unit) != profile.units.end();
}
} // namespace
std::optional<UiStyleNumber> read_ui_style_literal_number(std::string_view property,
                                                          std::string_view literal) {
    const auto profile = ui_style_literal_profile(property);
    const auto number = read_ui_style_number(literal);
    return profile && number && style_number_in_profile(*profile, *number) ? number : std::nullopt;
}
std::string format_ui_style_literal_number(std::string_view property, UiStyleNumber number) {
    const auto profile = ui_style_literal_profile(property);
    require(
        profile && style_number_in_profile(*profile, number),
        "Use a supported literal number, unit and range for this property; other CSS uses Value");
    return format_ui_style_number(number);
}
std::optional<UiStyleColor> read_ui_style_color(std::string_view text) {
    const auto range = trimmed(text, {0, text.size()});
    text = text.substr(range.begin, range.end - range.begin);
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#')
        return {};
    std::uint32_t packed = 0;
    const auto parsed = std::from_chars(text.data() + 1, text.data() + text.size(), packed, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return {};
    if (text.size() == 7)
        packed = (packed << 8) | 255;
    UiStyleColor result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<float>((packed >> (8 * (3 - i))) & 255) / 255.f;
    return result;
}
std::string format_ui_style_color(UiStyleColor color) {
    std::uint32_t packed = 0;
    for (const auto channel : color) {
        require(std::isfinite(channel) && channel >= 0 && channel <= 1,
                "Color channels must be finite values between 0 and 1");
        packed = (packed << 8) | static_cast<std::uint32_t>(std::lround(channel * 255));
    }
    const auto digits = (packed & 255) == 255 ? 6u : 8u;
    if (digits == 6)
        packed >>= 8;
    std::array<char, 8> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), packed, 16);
    require(result.ec == std::errc{}, "Could not format the style color");
    const auto count = static_cast<std::size_t>(result.ptr - buffer.data());
    return "#" + std::string(digits - count, '0') + std::string(buffer.data(), result.ptr);
}
namespace {
void valid_layout_boxes(std::span<const UiMarkupLayoutBox> boxes) {
    require(!boxes.empty() && boxes.size() <= ui_source_selection_limit,
            "Choose1..32 native layout members");
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto& box = boxes[i];
        valid_layout(box.source.pixels);
        for (const auto value :
             {box.bounds.left, box.bounds.top, box.bounds.width, box.bounds.height})
            require(std::isfinite(value), "Native group bounds must be finite");
        require(box.bounds.width > 0 && box.bounds.height > 0,
                "Native group bounds must have positive sizes");
        require(std::ranges::none_of(
                    boxes.first(i),
                    [&](const auto& other) { return other.source.element == box.source.element; }),
                "A layout group cannot repeat an element");
    }
}
} // namespace
std::vector<UiMarkupLayoutEdit> arrange_ui_markup_layouts(std::span<const UiMarkupLayoutBox> boxes,
                                                          std::string_view action, float dx,
                                                          float dy, float grid) {
    require(boxes.size() >= (action == "snap_grid" ? 1u : 2u) &&
                boxes.size() <= ui_source_selection_limit,
            "Choose2..32 group elements, or1..32 elements for grid alignment");
    require(std::ranges::any_of(ui_layout_group_actions,
                                [&](const auto& item) { return item.first == action; }),
            "Unknown group layout operation");
    require(std::isfinite(dx) && std::isfinite(dy), "Group movement must use finite pixels");
    require(action == "move" || (dx == 0 && dy == 0),
            "Only group movement accepts horizontal/vertical offsets");
    require(std::isfinite(grid) && grid >= 1 && grid <= 1024,
            "Grid spacing must be finite and within1..1024 pixels");
    require(action == "snap_grid" || grid == 8, "Grid spacing applies only to grid alignment");
    valid_layout_boxes(boxes);
    double left = std::numeric_limits<double>::max(), top = left;
    double right = std::numeric_limits<double>::lowest(), bottom = right;
    std::vector<UiMarkupLayoutEdit> result;
    result.reserve(boxes.size());
    for (const auto& box : boxes) {
        left = std::min(left, double(box.bounds.left));
        top = std::min(top, double(box.bounds.top));
        right = std::max(right, double(box.bounds.left) + box.bounds.width);
        bottom = std::max(bottom, double(box.bounds.top) + box.bounds.height);
        result.push_back(box.source);
    }
    const bool distribute_x = action == "distribute_horizontal";
    if (distribute_x || action == "distribute_vertical") {
        require(boxes.size() >= 3, "Distribution needs at least3 elements");
        std::vector<std::size_t> order(boxes.size());
        std::iota(order.begin(), order.end(), 0);
        const auto position = [&](std::size_t i) {
            return distribute_x ? boxes[i].bounds.left : boxes[i].bounds.top;
        };
        std::stable_sort(order.begin(), order.end(),
                         [&](auto a, auto b) { return position(a) < position(b); });
        double total_size = 0;
        for (const auto& box : boxes)
            total_size += distribute_x ? box.bounds.width : box.bounds.height;
        const double available = (distribute_x ? right - left : bottom - top) - total_size;
        // Match the source/native pixel admission precision at touching edges.
        require(available >= -.001,
                "Move outer elements apart before distributing non-overlapping gaps");
        const double gap = std::max(0., available) / double(boxes.size() - 1);
        double next = distribute_x ? left : top;
        for (const auto i : order) {
            auto& pixels = result[i].pixels;
            if (distribute_x)
                pixels.left = float(double(pixels.left) + (next - boxes[i].bounds.left));
            else
                pixels.top = float(double(pixels.top) + (next - boxes[i].bounds.top));
            next += (distribute_x ? boxes[i].bounds.width : boxes[i].bounds.height) + gap;
        }
    } else {
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            const auto& native = boxes[i].bounds;
            auto& pixels = result[i].pixels;
            double x = native.left, y = native.top;
            if (action == "move") {
                pixels.left += dx;
                pixels.top += dy;
                continue;
            } else if (action == "snap_grid") {
                x = std::round(double(native.left) / grid) * grid;
                y = std::round(double(native.top) / grid) * grid;
            } else if (action.starts_with("match_")) {
                const auto& reference = boxes.back().bounds;
                if (action != "match_height")
                    pixels.width = float(double(pixels.width) + reference.width - native.width);
                if (action != "match_width")
                    pixels.height = float(double(pixels.height) + reference.height - native.height);
                continue;
            } else if (action == "align_left")
                x = left;
            else if (action == "align_horizontal_center")
                x = (left + right - native.width) / 2;
            else if (action == "align_right")
                x = right - native.width;
            else if (action == "align_top")
                y = top;
            else if (action == "align_vertical_center")
                y = (top + bottom - native.height) / 2;
            else if (action == "align_bottom")
                y = bottom - native.height;
            pixels.left = float(double(pixels.left) + (x - native.left));
            pixels.top = float(double(pixels.top) + (y - native.top));
        }
    }
    for (const auto& change : result)
        valid_layout(change.pixels);
    return result;
}
std::vector<UiMarkupLayoutBox> transform_ui_markup_layouts(std::span<const UiMarkupLayoutBox> boxes,
                                                           std::string_view action, float dx,
                                                           float dy, std::optional<float> grid) {
    valid_layout_boxes(boxes);
    require(std::ranges::any_of(ui_layout_transform_actions,
                                [&](const auto& item) { return item.first == action; }),
            "Unknown layout transform");
    require(std::isfinite(dx) && std::isfinite(dy), "Layout deltas must be finite");
    if (grid)
        require(std::isfinite(*grid) && *grid >= 1 && *grid <= 1024,
                "Grid spacing must be finite and within1..1024 pixels");
    const bool resize = action == "resize" || action == "resize_aspect";
    require(!resize || boxes.size() == 1, "Resize one native layout member at a time");
    std::vector<UiMarkupLayoutBox> result(boxes.begin(), boxes.end());
    if (!resize) {
        const bool horizontal = std::abs(dx) >= std::abs(dy), axis = action == "move_axis";
        if (axis) {
            if (horizontal)
                dy = 0;
            else
                dx = 0;
        }
        float min_x = -131072, max_x = 131072, min_y = -131072, max_y = 131072;
        for (const auto& box : boxes) {
            min_x = std::max(min_x, -65536 - box.source.pixels.left);
            max_x = std::min(max_x, 65536 - box.source.pixels.left);
            min_y = std::max(min_y, -65536 - box.source.pixels.top);
            max_y = std::min(max_y, 65536 - box.source.pixels.top);
        }
        dx = std::clamp(dx, min_x, max_x);
        dy = std::clamp(dy, min_y, max_y);
        if (grid && (dx != 0 || dy != 0)) {
            const auto& anchor = boxes.back().bounds;
            if (!axis || horizontal)
                dx = float(std::round((double(anchor.left) + dx) / *grid) * *grid - anchor.left);
            if (!axis || !horizontal)
                dy = float(std::round((double(anchor.top) + dy) / *grid) * *grid - anchor.top);
        }
        for (auto& box : result) {
            box.source.pixels.left += dx;
            box.source.pixels.top += dy;
            box.bounds.left += dx;
            box.bounds.top += dy;
        }
    } else {
        const auto& before = boxes.front();
        auto& target = result.front();
        auto& pixels = target.source.pixels;
        if (action == "resize_aspect") {
            const double w = before.bounds.width, h = before.bounds.height;
            const bool horizontal = std::abs(double(dx) / w) >= std::abs(double(dy) / h);
            double scale = 1 + (horizontal ? double(dx) / w : double(dy) / h);
            if (grid && (dx != 0 || dy != 0)) {
                const double size = horizontal ? w : h;
                scale = std::round(size * scale / *grid) * *grid / size;
            }
            const double minimum =
                std::max(1 + (1 - double(pixels.width)) / w, 1 + (1 - double(pixels.height)) / h);
            const double maximum = std::min(1 + (65536 - double(pixels.width)) / w,
                                            1 + (65536 - double(pixels.height)) / h);
            scale = std::clamp(scale, minimum, maximum);
            pixels.width = float(double(pixels.width) + w * (scale - 1));
            pixels.height = float(double(pixels.height) + h * (scale - 1));
        } else {
            pixels.width = std::clamp(pixels.width + dx, 1.f, 65536.f);
            pixels.height = std::clamp(pixels.height + dy, 1.f, 65536.f);
            if (grid && (dx != 0 || dy != 0)) {
                const double w =
                    double(before.bounds.width) + pixels.width - before.source.pixels.width;
                const double h =
                    double(before.bounds.height) + pixels.height - before.source.pixels.height;
                pixels.width += float(std::round(w / *grid) * *grid - w);
                pixels.height += float(std::round(h / *grid) * *grid - h);
            }
        }
        target.bounds.width += pixels.width - before.source.pixels.width;
        target.bounds.height += pixels.height - before.source.pixels.height;
    }
    valid_layout_boxes(result);
    return result;
}
UiPixelLayout read_ui_markup_layout(std::string_view source, std::size_t index) {
    return read_ui_markup_layout(source, inspect_ui_markup(source), index);
}
UiPixelLayout read_ui_markup_layout(std::string_view source, std::span<const UiMarkupElement> nodes,
                                    std::size_t index) {
    return layout_source(source, nodes, index).pixels;
}
std::string set_ui_markup_layout(std::string_view source, std::size_t index, UiPixelLayout pixels) {
    const std::array edits{UiMarkupLayoutEdit{index, pixels}};
    return set_ui_markup_layouts(source, edits);
}
std::string set_ui_markup_layouts(std::string_view source,
                                  std::span<const UiMarkupLayoutEdit> changes) {
    require(!changes.empty() && changes.size() <= ui_source_selection_limit,
            "Choose1..32 elements for an atomic canvas layout edit");
    const auto nodes = inspect_ui_markup(source);
    std::vector<std::size_t> seen;
    std::vector<std::pair<UiSourceRange, std::string>> edits;
    seen.reserve(changes.size());
    edits.reserve(changes.size() * 4);
    for (const auto& change : changes) {
        require(std::ranges::find(seen, change.element) == seen.end(),
                "A canvas layout batch cannot repeat an element");
        seen.push_back(change.element);
        valid_layout(change.pixels);
        const auto original = layout_source(source, nodes, change.element);
        if (change.pixels == original.pixels)
            continue;
        const auto& pixels = change.pixels;
        const std::array values{pixels.left, pixels.top, pixels.width, pixels.height};
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::array<char, 128> buffer;
            const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                 values[i], std::chars_format::fixed);
            require(formatted.ec == std::errc{}, "Canvas pixel formatting failed");
            edits.emplace_back(original.values[i],
                               std::string(buffer.data(), formatted.ptr) + "px");
        }
    }
    return markup_edits(source, std::move(edits));
}
std::string set_ui_markup_attribute(std::string_view source, std::size_t index,
                                    std::string_view name, std::optional<std::string> value) {
    identifier(name);
    const auto nodes = inspect_ui_markup(source);
    const auto& node = element(nodes, index);
    const auto edit = attribute_edit(source, node, name, value);
    return replaced(source, edit.first, edit.second);
}
std::optional<std::string> read_ui_markup_style(std::string_view source,
                                                std::span<const UiMarkupElement> nodes,
                                                std::size_t index, std::string_view property) {
    identifier(property);
    const auto style = inline_style_source(source, element(nodes, index));
    if (const auto* found = style_property(style.text, style.declarations, property)) {
        const auto range = trimmed(style.text, found->value);
        return std::string(style.text.substr(range.begin, range.end - range.begin));
    }
    return {};
}
std::string set_ui_markup_style(std::string_view source, std::size_t index,
                                std::string_view property, std::optional<std::string> value) {
    const std::array indices{index};
    return set_ui_markup_styles(source, indices, property, std::move(value));
}
std::vector<std::optional<std::string>>
read_ui_markup_styles(std::string_view source, std::span<const UiMarkupElement> nodes,
                      std::span<const std::size_t> indices, std::string_view property) {
    (void)sibling_ranges(nodes, indices);
    std::vector<std::optional<std::string>> values;
    values.reserve(indices.size());
    for (const auto index : indices)
        values.push_back(read_ui_markup_style(source, nodes, index, property));
    return values;
}
std::string set_ui_markup_styles(std::string_view source, std::span<const std::size_t> indices,
                                 std::string_view property, std::optional<std::string> value) {
    identifier(property);
    const auto nodes = inspect_ui_markup(source);
    (void)sibling_ranges(nodes, indices);
    std::vector<SourceEdit> edits;
    edits.reserve(indices.size());
    for (const auto index : indices)
        edits.push_back(markup_style_edit(source, nodes[index], property, value));
    return markup_edits(source, std::move(edits));
}
std::vector<std::optional<std::string>>
read_ui_markup_classes(std::string_view source, std::span<const UiMarkupElement> nodes,
                       std::span<const std::size_t> indices) {
    (void)sibling_ranges(nodes, indices);
    std::vector<std::optional<std::string>> values;
    values.reserve(indices.size());
    for (const auto index : indices)
        values.push_back(class_value(source, nodes[index]));
    return values;
}
std::string set_ui_markup_classes(std::string_view source, std::span<const std::size_t> indices,
                                  std::optional<std::string> value) {
    if (value)
        literal_classes(*value);
    const auto nodes = inspect_ui_markup(source);
    (void)sibling_ranges(nodes, indices);
    std::vector<SourceEdit> edits;
    edits.reserve(indices.size());
    for (const auto index : indices) {
        const auto& node = nodes[index];
        if (class_value(source, node) == value)
            continue;
        edits.push_back(attribute_edit(source, node, "class", value));
    }
    return markup_edits(source, std::move(edits));
}
std::optional<std::string> read_ui_stylesheet_property(std::string_view source,
                                                       std::span<const UiStyleRule> rules,
                                                       std::size_t index,
                                                       std::string_view property) {
    identifier(property);
    const auto& rule = stylesheet_rule(rules, index);
    const auto text = source.substr(rule.content.begin, rule.content.end - rule.content.begin);
    const auto declarations = inspect_ui_style(text);
    if (const auto* found = style_property(text, declarations, property)) {
        const auto range = trimmed(text, found->value);
        return std::string(text.substr(range.begin, range.end - range.begin));
    }
    return {};
}
std::string set_ui_stylesheet_property(std::string_view source, std::size_t index,
                                       std::string_view property,
                                       std::optional<std::string> value) {
    const auto rules = inspect_ui_stylesheet(source);
    const auto& rule = stylesheet_rule(rules, index);
    const auto text = source.substr(rule.content.begin, rule.content.end - rule.content.begin);
    const auto declarations = inspect_ui_style(text);
    const auto edit = style_edit(text, declarations, property, value);
    return replaced(source,
                    {rule.content.begin + edit.first.begin, rule.content.begin + edit.first.end},
                    edit.second, false);
}
std::string set_ui_stylesheet_selector(std::string_view source, std::size_t index,
                                       std::string_view selector) {
    stylesheet_selector(selector);
    const auto rules = inspect_ui_stylesheet(source);
    return replaced(source, stylesheet_rule(rules, index).selector, selector, false);
}
std::string insert_ui_stylesheet_rule(std::string_view source, std::string_view selector) {
    stylesheet_selector(selector);
    require(inspect_ui_stylesheet(source).size() < 4096, "Stylesheet outline exceeds4096 blocks");
    const bool newline = !source.empty() && source.back() != '\n';
    return replaced(source, {source.size(), source.size()},
                    (newline ? "\n" : "") + std::string(selector) + " {}\n", false);
}
namespace {
struct StylesheetHead {
    std::vector<UiMarkupElement> nodes;
    std::optional<std::size_t> head;
};
StylesheetHead stylesheet_head(std::string_view source) {
    StylesheetHead result{inspect_ui_markup(source), {}};
    require(!result.nodes.empty() && result.nodes[0].tag == "rml" &&
                result.nodes[0].parent == UiMarkupElement::no_parent &&
                !result.nodes[0].self_closing,
            "Use one complete RML root for stylesheet links; other forms use Source");
    for (std::size_t i = 1; i < result.nodes.size(); ++i) {
        const auto& node = result.nodes[i];
        require(node.parent != UiMarkupElement::no_parent, "Multiple document roots use Source");
        if (node.tag == "head") {
            require(node.parent == 0 && !result.head,
                    "Use one direct document head; nested/duplicate heads use Source");
            result.head = i;
        }
    }
    return result;
}
std::optional<UiMarkupStylesheetLink>
stylesheet_link(std::string_view source, const StylesheetHead& context, std::size_t index) {
    const auto& node = element(context.nodes, index);
    if (node.tag != "link" || !context.head || node.parent != *context.head)
        return {};
    const auto literal = [&](std::string_view name) {
        const auto* attribute = markup_attribute(node, name);
        require(attribute && attribute->has_value,
                "Stylesheet links need literal type/href attributes; complete them in Source");
        auto text = Rml::StringUtilities::DecodeRml(std::string(
            source.substr(attribute->value.begin, attribute->value.end - attribute->value.begin)));
        require(text.find("{{") == text.npos && text.find("}}") == text.npos,
                "Bound stylesheet attributes use Source");
        return text;
    };
    const auto* type_attribute = markup_attribute(node, "type");
    if (!type_attribute || !type_attribute->has_value)
        return {};
    auto type = literal("type");
    const auto lower = Rml::StringUtilities::ToLower(type);
    if (lower != "text/rcss" && lower != "text/css")
        return {};
    require(std::ranges::none_of(
                node.attributes,
                [](const auto& attribute) { return attribute.name.starts_with("data-"); }),
            "Bound stylesheet links use Source");
    return UiMarkupStylesheetLink{index, literal("href"), std::move(type)};
}
std::vector<UiMarkupStylesheetLink> stylesheet_links(std::string_view source,
                                                     const StylesheetHead& context) {
    std::vector<UiMarkupStylesheetLink> result;
    for (std::size_t i = 0; i < context.nodes.size(); ++i)
        if (auto link = stylesheet_link(source, context, i)) {
            require(result.size() < 32, "Guided stylesheet links support at most32 entries");
            result.push_back(std::move(*link));
        }
    return result;
}
} // namespace
std::vector<UiMarkupStylesheetLink> read_ui_markup_stylesheet_links(std::string_view source) {
    const auto context = stylesheet_head(source);
    return stylesheet_links(source, context);
}
std::string set_ui_markup_stylesheet_link(std::string_view source, std::optional<std::size_t> index,
                                          std::string_view reference) {
    require(!reference.empty() && reference.size() <= 1024 &&
                reference.find_first_of(":\r\n?#") == reference.npos &&
                reference.find("{{") == reference.npos && reference.find("}}") == reference.npos &&
                std::filesystem::u8path(reference).is_relative(),
            "Use one literal document-relative RCSS reference");
    const auto path = std::filesystem::u8path(reference);
    require(Rml::StringUtilities::ToLower(path.extension().string()) == ".rcss",
            "Choose an RCSS stylesheet");
    const auto context = stylesheet_head(source);
    const auto links = stylesheet_links(source, context);
    if (index) {
        const auto found = std::ranges::find(links, *index, &UiMarkupStylesheetLink::element);
        require(found != links.end(), "Choose a literal stylesheet link in the document head");
        if (found->reference == reference)
            return std::string(source);
        const auto* attribute = markup_attribute(context.nodes[*index], "href");
        return replaced(source, attribute->source, "href=\"" + escaped(reference) + '"');
    }
    require(links.size() < 32, "Guided stylesheet links support at most32 entries");
    const auto link = "<link type=\"text/rcss\" href=\"" + escaped(reference) + "\" />";
    if (!context.head) {
        const auto position = context.nodes[0].content.begin;
        return replaced(source, {position, position}, "<head>\n" + link + "\n</head>");
    }
    const auto& head = context.nodes[*context.head];
    if (head.self_closing)
        return replaced(source, {head.opening.end - 2, head.opening.end},
                        ">\n" + link + "\n</head>");
    return replaced(source, {head.content.end, head.content.end}, "\n" + link + "\n");
}
std::string remove_ui_markup_stylesheet_link(std::string_view source, std::size_t index) {
    const auto context = stylesheet_head(source);
    require(stylesheet_link(source, context, index).has_value(),
            "Choose a literal stylesheet link in the document head");
    return replaced(source, context.nodes[index].source, {});
}
std::string move_ui_markup_stylesheet_link(std::string_view source, std::size_t index,
                                           int direction) {
    require(direction == -1 || direction == 1, "Move a stylesheet link up or down");
    const auto context = stylesheet_head(source);
    require(stylesheet_link(source, context, index).has_value(),
            "Choose a literal stylesheet link in the document head");
    std::vector<std::size_t> siblings;
    for (std::size_t i = 0; i < context.nodes.size(); ++i)
        if (context.nodes[i].parent == *context.head)
            siblings.push_back(i);
    const auto found = std::ranges::find(siblings, index);
    const auto position = std::size_t(found - siblings.begin());
    require(direction < 0 ? position > 0 : position + 1 < siblings.size(),
            "The stylesheet link is already at this head boundary");
    const auto other = siblings[direction < 0 ? position - 1 : position + 1];
    require(stylesheet_link(source, context, other).has_value(),
            "Move across other head content in Source");
    const auto first = context.nodes[std::min(index, other)].source;
    const auto last = context.nodes[std::max(index, other)].source;
    const auto text = std::string(source.substr(last.begin, last.end - last.begin)) +
                      std::string(source.substr(first.end, last.begin - first.end)) +
                      std::string(source.substr(first.begin, first.end - first.begin));
    return replaced(source, {first.begin, last.end}, text);
}
namespace {
const UiStyleRule& font_face_rule(std::string_view source, std::span<const UiStyleRule> rules,
                                  std::size_t index) {
    require(index < rules.size(), "Choose a font face in the current stylesheet");
    const auto& rule = rules[index];
    const auto selector = trimmed(source, rule.selector);
    require(source.substr(selector.begin, selector.end - selector.begin) == "@font-face",
            "Choose a literal top-level @font-face declaration; other at-rules use Source");
    return rule;
}
} // namespace
std::map<std::string, std::string> read_ui_stylesheet_font_face(std::string_view source,
                                                                std::size_t index) {
    const auto rules = inspect_ui_stylesheet(source);
    const auto& rule = font_face_rule(source, rules, index);
    const auto text = source.substr(rule.content.begin, rule.content.end - rule.content.begin);
    const auto declarations = inspect_ui_style(text);
    std::map<std::string, std::string> result;
    for (const auto* name : {"font-family", "src", "-rmlui-face-index", "font-weight", "font-style",
                             "-rmlui-fallback-face"})
        if (const auto* found = style_property(text, declarations, name)) {
            const auto range = trimmed(text, found->value);
            result[name] = std::string(text.substr(range.begin, range.end - range.begin));
        }
    return result;
}
std::string set_ui_stylesheet_font_face(std::string_view source, std::optional<std::size_t> index,
                                        std::string_view family, std::string_view font_source,
                                        unsigned face_index) {
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    require(!family.empty() && family.size() <= 64 &&
                (letter(family.front()) || family.front() == '_') &&
                std::ranges::all_of(family,
                                    [&](char c) {
                                        return letter(c) || (c >= '0' && c <= '9') || c == '_' ||
                                               c == '-';
                                    }),
            "Use a plain family alias up to64 letters/digits/_/-, starting with a letter or _");
    const auto path = ProjectPaths::normalize(std::filesystem::u8path(font_source));
    require(ui_font_detail::extension(Rml::StringUtilities::ToLower(path.extension().string())),
            "Choose a project TTF, OTF, TTC or OTC font source");
    require(face_index < ui_font_detail::face_limit, "Use a zero-based font face index below32");
    const auto rules = inspect_ui_stylesheet(source);
    std::string body;
    UiSourceRange range{source.size(), source.size()};
    if (index) {
        range = font_face_rule(source, rules, *index).content;
        body = source.substr(range.begin, range.end - range.begin);
    } else
        require(rules.size() < 4096, "Stylesheet outline exceeds4096 blocks");
    const auto source_path = path_utf8(path);
    const std::array<std::pair<std::string, std::string>, 3> values{
        {{"font-family", std::string(family)},
         {"src", "\"" + source_path + "\""},
         {"-rmlui-face-index", std::to_string(face_index)}}};
    const auto same_literal = [](std::string_view raw, std::string_view literal) {
        return raw == literal ||
               (raw.size() == literal.size() + 2 && (raw.front() == '\'' || raw.front() == '"') &&
                raw.back() == raw.front() && raw.substr(1, raw.size() - 2) == literal);
    };
    for (const auto& [name, value] : values) {
        const auto declarations = inspect_ui_style(body);
        if (const auto* existing = style_property(body, declarations, name)) {
            const auto range = trimmed(body, existing->value);
            const std::string_view raw =
                std::string_view(body).substr(range.begin, range.end - range.begin);
            if ((name == "font-family" && same_literal(raw, family)) ||
                (name == "src" && same_literal(raw, source_path) &&
                 (raw != source_path ||
                  source_path.find_first_of(" \t\r\n,()'\"\\") == source_path.npos)))
                continue;
            if (name == "-rmlui-face-index") {
                int previous = -1;
                const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), previous);
                if (parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size() &&
                    previous >= 0 && unsigned(previous) == face_index)
                    continue;
            }
        } else if (index && name == "-rmlui-face-index" && face_index == 0)
            continue; // Preserve the existing omitted native default.
        const auto edit = style_value_edit(body, declarations, name, value);
        body.replace(edit.first.begin, edit.first.end - edit.first.begin, edit.second);
    }
    if (index)
        return replaced(source, range, body, false);
    const bool newline = !source.empty() && source.back() != '\n';
    return replaced(source, range,
                    (newline ? "\n" : "") + std::string("@font-face {") + body + "}\n", false);
}
std::string remove_ui_stylesheet_rule(std::string_view source, std::size_t index) {
    const auto rules = inspect_ui_stylesheet(source);
    return replaced(source, stylesheet_rule(rules, index).source, {}, false);
}
std::string remove_ui_stylesheet_font_face(std::string_view source, std::size_t index) {
    const auto rules = inspect_ui_stylesheet(source);
    return replaced(source, font_face_rule(source, rules, index).source, {}, false);
}
std::string set_ui_stylesheet_font_face_style(
    std::string_view source, std::size_t index,
    const std::map<std::string, std::optional<std::string>>& properties) {
    const auto rules = inspect_ui_stylesheet(source);
    const auto range = font_face_rule(source, rules, index).content;
    std::string body(source.substr(range.begin, range.end - range.begin));
    for (const auto& [name, value] : properties) {
        const bool fallback = name == "-rmlui-fallback-face";
        require(name == "font-weight" || name == "font-style" || fallback,
                "Use a supported font declaration property");
        std::optional<UiStyleNumber> number;
        if (value && fallback)
            require(*value == "true" || *value == "false", "Use true or false for font fallback");
        else if (value) {
            const auto profile = ui_style_literal_profile(name);
            const bool keyword =
                std::ranges::find(profile->keywords, *value) != profile->keywords.end();
            if (name == "font-weight" && *value != "all" && !keyword) {
                number = read_ui_style_number(*value);
                require(number.has_value(),
                        "Use all, normal, bold or an integer font weight1..1000");
                (void)format_ui_style_literal_number(name, *number);
            } else
                require(keyword || (name == "font-weight" && *value == "all"),
                        "Use a supported font declaration style or weight");
        }
        const auto declarations = inspect_ui_style(body);
        if (const auto* existing = style_property(body, declarations, name); existing && value) {
            const auto old_range = trimmed(body, existing->value);
            const auto old =
                std::string_view(body).substr(old_range.begin, old_range.end - old_range.begin);
            if (old == *value || (number && read_ui_style_number(old) == number))
                continue;
        }
        const auto edit = style_value_edit(body, declarations, name, value);
        body.replace(edit.first.begin, edit.first.end - edit.first.begin, edit.second);
    }
    return replaced(source, range, body, false);
}
std::string duplicate_ui_stylesheet_rule(std::string_view source, std::size_t index) {
    const auto rules = inspect_ui_stylesheet(source);
    const auto& rule = stylesheet_rule(rules, index);
    require(rules.size() < 4096, "Stylesheet outline exceeds4096 blocks");
    return replaced(
        source, {rule.source.end, rule.source.end},
        "\n" + std::string(source.substr(rule.source.begin, rule.source.end - rule.source.begin)),
        false);
}
std::string move_ui_stylesheet_rule(std::string_view source, std::size_t index, int direction) {
    require(direction == -1 || direction == 1, "Move a stylesheet rule up or down by one");
    const auto rules = inspect_ui_stylesheet(source);
    (void)stylesheet_rule(rules, index);
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 == rules.size()))
        return std::string(source);
    const auto other = direction < 0 ? index - 1 : index + 1;
    (void)stylesheet_rule(rules, other);
    const auto first = rules[std::min(index, other)].source;
    const auto second = rules[std::max(index, other)].source;
    const auto a = source.substr(first.begin, first.end - first.begin);
    const auto between = source.substr(first.end, second.begin - first.end);
    const auto b = source.substr(second.begin, second.end - second.begin);
    return replaced(source, {first.begin, second.end},
                    std::string(b) + std::string(between) + std::string(a), false);
}
std::string set_ui_markup_text(std::string_view source, std::size_t index,
                               std::string_view literal) {
    const auto nodes = inspect_ui_markup(source);
    const auto& node = element(nodes, index);
    editable(node);
    require(!node.self_closing, "This element cannot contain text");
    require(
        std::ranges::none_of(nodes, [index](const auto& child) { return child.parent == index; }),
        "Select a leaf element to replace its text without removing child widgets");
    require(source.substr(node.content.begin, node.content.end - node.content.begin).find("<!--") ==
                std::string_view::npos,
            "This element contains comments; edit its text in the source view");
    return replaced(source, node.content, escaped(literal));
}
std::string insert_ui_markup_child(std::string_view source, std::size_t parent,
                                   std::string_view tag, std::string_view literal,
                                   std::optional<std::size_t> before) {
    identifier(tag);
    const auto nodes = inspect_ui_markup(source);
    const auto& node = element(nodes, parent);
    if (node.tag != "body")
        editable(node);
    require(!node.self_closing, "Choose a container element for a new child");
    auto position = node.closing.begin;
    if (before) {
        const auto& next = element(nodes, *before);
        require(next.parent == parent, "Insertion target must be a direct child of this container");
        position = next.source.begin;
    }
    UiMarkupElement candidate;
    candidate.tag = tag;
    editable(candidate);
    const bool single = tag == "img" || tag == "input" || tag == "br" || tag == "hr";
    require(!single || literal.empty(), "This element cannot contain text");
    const auto markup =
        single ? "<" + std::string(tag) + "/>"
               : "<" + std::string(tag) + ">" + escaped(literal) + "</" + std::string(tag) + ">";
    return replaced(source, {position, position}, markup);
}
std::string remove_ui_markup_element(std::string_view source, std::size_t index) {
    const std::array indices{index};
    return remove_ui_markup_elements(source, indices);
}
UiMarkupControlEdit insert_ui_markup_control(std::string_view source, std::size_t parent,
                                             std::string_view kind, std::string_view literal,
                                             std::optional<std::size_t> before, std::string_view id,
                                             std::optional<UiPixelLayout> pixels) {
    const auto found = std::ranges::find(ui_markup_control_kinds, kind, &UiMarkupControlKind::id);
    require(found != ui_markup_control_kinds.end(), "Choose a supported UI control kind");
    require(literal.size() <= 8192, "Initial UI text/value is limited to 8192 bytes");
    require(found->accepts_text() || literal.empty(), "This control has no initial text/value");
    if (found->input_type == "range") {
        if (literal.empty())
            literal = "0";
        (void)parse_ui_range_number(literal);
    }
    const auto nodes = inspect_ui_markup(source);
    const auto& container = element(nodes, parent);
    require(container.tag != "textarea" && container.tag != "option" && container.tag != "progress",
            "Choose a container outside text areas, dropdown options and progress controls");
    require((found->tag == "option") == (container.tag == "select"),
            "Dropdowns contain dropdown options; insert an option inside a select element");
    const auto position = before ? element(nodes, *before).source.begin : container.closing.begin;
    const auto index = static_cast<std::size_t>(std::ranges::count_if(
        nodes, [&](const auto& node) { return node.source.begin < position; }));
    const bool input = found->tag == "input", select = found->tag == "select";
    auto candidate = insert_ui_markup_child(source, parent, found->tag,
                                            input || select ? std::string_view{} : literal, before);
    if (input) {
        candidate =
            set_ui_markup_attribute(candidate, index, "type", std::string(found->input_type));
        if (!literal.empty())
            candidate = set_ui_markup_attribute(candidate, index, "value", std::string(literal));
    } else if (select && !literal.empty()) {
        candidate = insert_ui_markup_child(candidate, index, "option", literal);
        candidate = set_ui_markup_attribute(candidate, index + 1, "value", std::string(literal));
        candidate = set_ui_markup_attribute(candidate, index + 1, "selected", "selected");
    } else if (found->tag == "option" && !literal.empty())
        candidate = set_ui_markup_attribute(candidate, index, "value", std::string(literal));
    else if (found->tag == "progress")
        candidate = set_ui_markup_attribute(candidate, index, "value", "0");
    if (!id.empty()) {
        candidate = set_ui_markup_attribute(candidate, index, "id", std::string(id));
        (void)literal_ids(candidate, inspect_ui_markup(candidate));
    }
    if (pixels) {
        require(!id.empty(), "Pixel control creation requires a unique source ID");
        require(found->tag != "br" && found->tag != "option",
                "Create breaks and dropdown options in their native flow layout");
        valid_layout(*pixels);
        candidate = set_ui_markup_style(candidate, index, "position", "absolute");
        const std::array values{pixels->left, pixels->top, pixels->width, pixels->height};
        constexpr std::array names{"left", "top", "width", "height"};
        for (std::size_t i = 0; i < values.size(); ++i)
            candidate =
                set_ui_markup_style(candidate, index, names[i],
                                    format_ui_style_number({values[i], UiStyleUnit::Pixels}));
        (void)read_ui_markup_layout(candidate, index);
    }
    return {std::move(candidate), index};
}
namespace {
bool control_style_property(std::string_view name) {
    return name == "focus" || name == "tab-index";
}
std::vector<UiMarkupControlProperty> control_property_metadata(std::string_view kind) {
    std::vector<UiMarkupControlProperty> result;
    const auto add = [&](const char* name, const char* label, UiControlPropertyType type,
                         UiControlPropertyValue value, std::vector<std::string> choices = {}) {
        UiMarkupControlProperty row;
        row.name = name;
        row.label = label;
        row.type = type;
        row.value = std::move(value);
        row.choices = std::move(choices);
        if (type == UiControlPropertyType::Number) {
            row.minimum =
                std::string_view(name) == "step" ? ui_range_minimum_step : -ui_range_magnitude;
            row.maximum = ui_range_magnitude;
        }
        result.push_back(std::move(row));
    };
    using Type = UiControlPropertyType;
    const auto integer = [&](const char* name, const char* label, double value, double minimum,
                             double maximum) {
        add(name, label, Type::Number, value);
        auto& row = result.back();
        row.minimum = minimum;
        row.maximum = maximum;
        row.integer = true;
    };
    if (kind == "text-input" || kind == "password-input" || kind == "textarea") {
        if (kind != "textarea")
            add("value",
                kind == "password-input" ? "Initial source value (visible)" : "Initial value",
                Type::Text, std::string{});
        add("placeholder", "Placeholder", Type::Text, std::string{});
        integer("maxlength", "Maximum length (-1: unlimited)", -1, -1, 1000000);
        if (kind == "textarea") {
            integer("cols", "Columns", 20, 1, 4096);
            integer("rows", "Rows", 2, 1, 4096);
            add("wrap", "Line wrapping", Type::Choice, std::string("wrap"), {"wrap", "nowrap"});
        } else
            integer("size", "Width in characters", 20, 1, 4096);
    } else if (kind == "image") {
        add("src", "Image source (document relative)", Type::Text, std::string{});
        add("sprite", "Sprite name (takes precedence)", Type::Text, std::string{});
        add("rect", "Crop rectangle (x y width height)", Type::Text, std::string{});
        for (const auto* name : {"width", "height"}) {
            add(name,
                std::string_view(name) == "width" ? "Width attribute (0: intrinsic)"
                                                  : "Height attribute (0: intrinsic)",
                Type::Number, std::monostate{});
            result.back().minimum = 0;
            result.back().minimum_positive = ui_range_minimum_step;
        }
    } else if (kind == "label") {
        add("for", "Target ID (empty: first child control)", Type::Text, std::string{});
    } else if (kind == "range") {
        add("value", "Initial value", Type::Number, 0.0);
        add("min", "Minimum", Type::Number, 0.0);
        add("max", "Maximum", Type::Number, 100.0);
        add("step", "Step", Type::Number, 1.0);
        add("orientation", "Orientation", Type::Choice, std::string("horizontal"),
            {"horizontal", "vertical"});
    } else if (kind == "progress") {
        add("value", "Authored value", Type::Number, 0.0);
        add("max", "Maximum", Type::Number, 1.0);
    } else if (kind == "checkbox" || kind == "radio" || kind == "option" ||
               kind == "input-button") {
        add("value", kind == "input-button" ? "Caption" : "Value", Type::Text, std::string{});
        if (kind == "radio")
            add("name", "Radio group", Type::Text, std::string{});
        if (kind == "checkbox" || kind == "radio")
            add("checked", "Initially checked", Type::Flag, false);
        if (kind == "option")
            add("selected", "Initially selected", Type::Flag, false);
    } else if (kind != "select" && kind != "button") {
        return {};
    }
    if (kind != "progress" && kind != "button" && kind != "label" && kind != "image")
        add("disabled", "Disabled", Type::Flag, false);
    if (kind != "progress" && kind != "option" && kind != "label" && kind != "image") {
        add("tab-index", "Keyboard tab stop", Type::Choice, std::monostate{}, {"auto", "none"});
        add("focus", "Focus", Type::Choice, std::monostate{}, {"auto", "none"});
    }
    return result;
}
std::string control_attribute_text(std::string_view source, const UiMarkupAttribute& attribute) {
    require(attribute.has_value, "This attribute needs a literal value or the Source view");
    return Rml::StringUtilities::DecodeRml(std::string(
        source.substr(attribute.value.begin, attribute.value.end - attribute.value.begin)));
}
std::string_view trimmed_control_literal(std::string_view value) {
    const auto range = trimmed(value, {0, value.size()});
    return value.substr(range.begin, range.end - range.begin);
}
void validate_control_number(const UiMarkupControlProperty& row, double value) {
    require(ui_range_number(value), "Use a finite control number within -1000000..1000000");
    const double normalized = static_cast<float>(value);
    require(!row.minimum || normalized >= *row.minimum, "Control number is below its minimum");
    require(!row.maximum || normalized <= *row.maximum, "Control number is above its maximum");
    require(!row.minimum_positive || value == 0 || normalized >= *row.minimum_positive,
            "A positive image dimension must be at least 0.000001");
    require(!row.integer || std::trunc(value) == value, "Use a whole-number control property");
}
void validate_image_rectangle(std::string_view literal) {
    literal = trimmed_control_literal(literal);
    if (literal.empty())
        return; // An empty attribute leaves the full image available to the native owner.
    std::array<float, 4> values{};
    for (auto& value : values) {
        const auto first = literal.find_first_not_of(' ');
        require(first != literal.npos, "Use four space-separated crop numbers: x y width height");
        literal.remove_prefix(first);
        const auto end = literal.find(' ');
        value = parse_ui_range_number(trimmed_control_literal(literal.substr(0, end)));
        literal = end == literal.npos ? std::string_view{} : literal.substr(end + 1);
    }
    require(trimmed_control_literal(literal).empty(),
            "Use exactly four crop numbers: x y width height");
    require(values[2] >= ui_range_minimum_step && values[3] >= ui_range_minimum_step,
            "Crop width and height must be at least 0.000001");
    const auto size =
        Rml::Rectanglef::FromPositionSize({values[0], values[1]}, {values[2], values[3]}).Size();
    require(size.x > 0 && size.y > 0,
            "Crop edges must remain distinct at native float precision; increase the size");
}
} // namespace
std::vector<std::string> ui_markup_control_property_names() {
    std::vector<std::string> result;
    for (const auto& kind : ui_markup_control_kinds)
        for (const auto& property : control_property_metadata(kind.id))
            if (std::find(result.begin(), result.end(), property.name) == result.end())
                result.push_back(property.name);
    std::sort(result.begin(), result.end());
    return result;
}
UiMarkupControlProperties read_ui_markup_control_properties(std::string_view source,
                                                            std::span<const UiMarkupElement> nodes,
                                                            std::size_t index) {
    const auto& node = element(nodes, index);
    UiMarkupControlProperties result;
    std::string input_type;
    try {
        if (node.tag == "input") {
            const auto* type = markup_attribute(node, "type");
            input_type = type ? control_attribute_text(source, *type) : "text";
        }
    } catch (const std::exception& error) {
        result.reason = error.what();
        return result;
    }
    const auto kind = std::find_if(
        ui_markup_control_kinds.begin(), ui_markup_control_kinds.end(),
        [&](const auto& item) { return item.tag == node.tag && item.input_type == input_type; });
    if (kind != ui_markup_control_kinds.end()) {
        result.kind = kind->id;
        result.label = kind->label;
        result.properties = control_property_metadata(kind->id);
    }
    if (node.tag == "option" &&
        (node.parent >= nodes.size() || nodes[node.parent].tag != "select")) {
        result.properties.clear();
        result.reason = "Dropdown option properties require a select parent; use Source";
        return result;
    }
    if (result.properties.empty()) {
        result.reason = "Edit this element's properties through Attributes or Source";
        return result;
    }
    const auto has = [&](const UiMarkupElement& target, std::string_view name) {
        return std::any_of(target.attributes.begin(), target.attributes.end(),
                           [&](const auto& attribute) { return attribute.name == name; });
    };
    for (auto& row : result.properties) {
        const bool style = control_style_property(row.name);
        const auto binding = std::string(style ? "data-style-" : "data-attr-") + row.name;
        const bool bound_value =
            (row.name == "value" || row.name == "checked" || row.name == "selected") &&
            has(node, "data-value");
        const bool bound_option = row.name == "selected" && node.tag == "option" &&
                                  node.parent < nodes.size() &&
                                  has(nodes[node.parent], "data-value");
        row.authored = !style && has(node, row.name);
        std::optional<std::string> literal;
        try {
            if (style)
                literal = read_ui_markup_style(source, nodes, index, row.name);
            else if (const auto* attribute = markup_attribute(node, row.name)) {
                row.authored = true;
                if (row.type != UiControlPropertyType::Flag)
                    literal = control_attribute_text(source, *attribute);
            }
            if (style)
                row.authored = literal.has_value();
        } catch (const std::exception& error) {
            row.value = std::monostate{};
            row.reason = error.what();
            continue;
        }
        if ((node.tag == "input" && has(node, "data-attr-type")) || has(node, binding) ||
            bound_value || bound_option || (style && has(node, "data-attr-style"))) {
            row.value = std::monostate{};
            row.reason = "This property has a source binding; edit it through Attributes or Source";
            continue;
        }
        row.editable = true;
        if (literal &&
            (literal->find("{{") != literal->npos || literal->find("}}") != literal->npos)) {
            row.value = std::monostate{};
            row.editable = false;
            row.reason = "Dynamic property expressions use the Source view";
            continue;
        }
        try {
            if (row.type == UiControlPropertyType::Number && !literal &&
                std::holds_alternative<std::monostate>(row.value)) {
                row.reason = "Intrinsic dimensions require the native image/sprite; enter an "
                             "attribute override or keep it absent. CSS can override sizing.";
                continue;
            }
            if (row.type == UiControlPropertyType::Flag)
                row.value = row.authored;
            else if (literal) {
                if (row.type == UiControlPropertyType::Number) {
                    const auto text = trimmed_control_literal(*literal);
                    double value = 0;
                    if (row.integer) {
                        int parsed = 0;
                        const auto number =
                            std::from_chars(text.data(), text.data() + text.size(), parsed);
                        require(
                            number.ec == std::errc{} && number.ptr == text.data() + text.size(),
                            "This integer literal uses Source; enter a whole-number replacement");
                        value = parsed;
                    } else
                        value = parse_ui_range_number(text);
                    validate_control_number(row, value);
                    row.value = value;
                } else if (row.type == UiControlPropertyType::Choice) {
                    const std::string value(trimmed_control_literal(*literal));
                    require(std::find(row.choices.begin(), row.choices.end(), value) !=
                                row.choices.end(),
                            "This custom property value uses Source; choose a supported value to "
                            "replace it");
                    row.value = value;
                } else {
                    require(literal->size() <= 8192 && literal->find('\0') == literal->npos,
                            "Literal control values above 8192 bytes or containing NUL use Source");
                    if (result.kind == "image" && row.name == "rect")
                        validate_image_rectangle(*literal);
                    row.value = *literal;
                }
            }
            row.available = true;
        } catch (const std::exception& error) {
            row.value = std::monostate{};
            row.reason = error.what();
        }
    }
    return result;
}
std::string
set_ui_markup_control_properties(std::string_view source, std::size_t index,
                                 const std::map<std::string, UiControlPropertyValue>& properties) {
    const auto nodes = inspect_ui_markup(source);
    const auto current = read_ui_markup_control_properties(source, nodes, index);
    require(!current.properties.empty(), "Choose a supported source control");
    std::string candidate(source);
    for (const auto& [name, value] : properties) {
        const auto row = std::find_if(current.properties.begin(), current.properties.end(),
                                      [&](const auto& item) { return item.name == name; });
        require(row != current.properties.end(),
                "This property is not supported by the selected source control");
        require(row->editable, "Bound or ambiguous control properties use Attributes or Source");
        std::optional<std::string> encoded;
        if (std::holds_alternative<std::monostate>(value)) {
            if (!row->authored)
                continue;
        } else if (row->type == UiControlPropertyType::Flag) {
            const auto* flag = std::get_if<bool>(&value);
            require(flag != nullptr, "Use a boolean control flag, or null to remove it");
            if (*flag == row->authored)
                continue;
            if (*flag)
                encoded = name;
        } else if (row->type == UiControlPropertyType::Number) {
            const auto* number = std::get_if<double>(&value);
            require(number != nullptr, "Use a numeric control property, or null to remove it");
            validate_control_number(*row, *number);
            const double normalized = static_cast<float>(*number);
            if (row->authored && row->available && row->value == UiControlPropertyValue(normalized))
                continue;
            encoded = format_ui_range_number(*number);
        } else {
            const auto* text = std::get_if<std::string>(&value);
            require(text != nullptr, "Use a literal string control property, or null to remove it");
            require(
                text->size() <= 8192 && text->find('\0') == text->npos &&
                    text->find("{{") == text->npos && text->find("}}") == text->npos,
                "Use at most8192 bytes of literal control text without NUL or dynamic expressions");
            if (row->type == UiControlPropertyType::Choice)
                require(std::find(row->choices.begin(), row->choices.end(), *text) !=
                            row->choices.end(),
                        "Choose a supported control property value");
            if (current.kind == "image" && name == "rect")
                validate_image_rectangle(*text);
            if (row->authored && row->available && row->value == value)
                continue;
            encoded = *text;
        }
        candidate = control_style_property(name)
                        ? set_ui_markup_style(candidate, index, name, std::move(encoded))
                        : set_ui_markup_attribute(candidate, index, name, std::move(encoded));
    }
    return candidate;
}

std::string remove_ui_markup_elements(std::string_view source,
                                      std::span<const std::size_t> indices) {
    const auto nodes = inspect_ui_markup(source);
    auto result = without_ranges(source, sibling_ranges(nodes, indices));
    validate_ui_text(result, true);
    return result;
}
std::string reparent_ui_markup_elements(std::string_view source,
                                        std::span<const std::size_t> indices, std::size_t parent,
                                        std::optional<std::size_t> before) {
    const auto nodes = inspect_ui_markup(source);
    const auto ranges = sibling_ranges(nodes, indices);
    const auto& container = element(nodes, parent);
    if (container.tag != "body")
        editable(container);
    require(!container.self_closing, "Choose a container for the moved source");
    static_ancestry(nodes, parent);
    const auto ids = literal_ids(source, nodes);
    auto position = container.closing.begin;
    if (before) {
        const auto& next = element(nodes, *before);
        require(next.parent == parent, "Placement target must be a direct child of the container");
        require(std::ranges::find(indices, *before) == indices.end(),
                "Choose a placement target outside the moved selection");
        position = next.source.begin;
    }
    for (const auto index : indices) {
        const auto& root = nodes[index];
        require(!contains(root.source, container.source),
                "A source subtree cannot move into itself or its descendants");
        static_subtree(source, nodes, index);
        for (const auto& [id, range] : ids)
            if (contains(root.source, range))
                require(id != "#self" && id != "#document" && id != "#parent",
                        "Reserved native lookup tokens cannot be moved as UI IDs");
        for (const auto& node : nodes)
            if (contains(root.source, node.source) && node.tag == "label")
                for (const auto& attr : node.attributes)
                    if (attr.name == "for" && attr.has_value) {
                        const auto target = Rml::StringUtilities::DecodeRml(std::string(
                            source.substr(attr.value.begin, attr.value.end - attr.value.begin)));
                        require(target != "#self" && target != "#document" && target != "#parent",
                                "Move relative native label targets explicitly in Source");
                    }
    }
    std::string moved;
    for (auto range = ranges.rbegin(); range != ranges.rend(); ++range)
        moved += source.substr(range->begin, range->end - range->begin);
    auto adjusted = position;
    for (const auto range : ranges) {
        require(position <= range.begin || position >= range.end,
                "A source insertion cannot be inside a moved subtree");
        if (range.end <= position)
            adjusted -= range.end - range.begin;
    }
    auto result = without_ranges(source, ranges);
    result.insert(adjusted, moved);
    validate_ui_text(result, true);
    return result;
}
std::string duplicate_ui_markup_element(std::string_view source, std::size_t index,
                                        std::string_view prefix) {
    const auto nodes = inspect_ui_markup(source);
    const auto& root = element(nodes, index);
    editable(root);
    static_subtree(source, nodes, index);
    const auto inside = [&](const UiMarkupElement& node) {
        return contains(root.source, node.source);
    };
    const auto fragment = source.substr(root.source.begin, root.source.end - root.source.begin);
    const auto existing = literal_ids(source, nodes);
    std::map<std::string, UiSourceRange> copied;
    bool root_has_id = false;
    for (const auto& [id, range] : existing)
        if (contains(root.source, range)) {
            require(id != "#self" && id != "#document" && id != "#parent",
                    "Reserved native lookup tokens cannot be copied as UI IDs");
            copied.emplace(id, range);
            root_has_id = root_has_id || contains(root.opening, range);
        }
    std::map<std::string, std::string> renamed;
    std::string chosen;
    const auto choose = [&](std::string candidate) {
        identifier(candidate);
        renamed.clear();
        if (!root_has_id && existing.contains(candidate))
            return false;
        for (const auto& [id, range] : copied) {
            auto name = candidate + "-" + id;
            require(name.size() <= 128, "Choose a shorter prefix or shorten the original UI IDs");
            if (existing.contains(name))
                return false;
            renamed.emplace(id, std::move(name));
        }
        chosen = std::move(candidate);
        return true;
    };
    bool available = false;
    if (!prefix.empty())
        available = choose(std::string(prefix));
    else
        for (unsigned attempt = 1; attempt <= 64 && !available; ++attempt)
            available = choose(attempt == 1 ? "copy" : "copy" + std::to_string(attempt));
    require(available, "Choose a prefix whose copied UI IDs are not already used");
    std::vector<std::pair<UiSourceRange, std::string>> edits;
    for (const auto& [id, range] : copied)
        edits.emplace_back(range, "id=\"" + escaped(renamed.at(id)) + "\"");
    if (!root_has_id) {
        const auto position = attribute_insertion(source, root);
        edits.emplace_back(UiSourceRange{position, position}, " id=\"" + escaped(chosen) + "\"");
    }
    for (const auto& node : nodes)
        if (inside(node) && node.tag == "label") {
            bool has_target = false;
            for (const auto& attr : node.attributes)
                if (attr.name == "for") {
                    require(!has_target, "Resolve duplicate label targets before duplication");
                    has_target = true;
                    require(attr.has_value, "Use a literal label target before duplication");
                    const auto target = Rml::StringUtilities::DecodeRml(std::string(
                        source.substr(attr.value.begin, attr.value.end - attr.value.begin)));
                    require(target.find("{{") == target.npos,
                            "Duplicate label target expressions explicitly in Source");
                    if (const auto found = renamed.find(target); found != renamed.end())
                        edits.emplace_back(attr.source, "for=\"" + escaped(found->second) + "\"");
                }
        }
    std::ranges::sort(edits,
                      [](const auto& a, const auto& b) { return a.first.begin > b.first.begin; });
    std::string clone(fragment);
    std::size_t next = root.source.end;
    for (const auto& [range, value] : edits) {
        require(range.begin >= root.source.begin && range.end <= next,
                "Copied UI attribute ranges overlap");
        next = range.begin;
        clone.replace(range.begin - root.source.begin, range.end - range.begin, value);
    }
    return replaced(source, {root.source.end, root.source.end}, clone);
}
std::string move_ui_markup_element(std::string_view source, std::size_t index, int direction) {
    require(direction == -1 || direction == 1, "Choose the previous or next sibling");
    const auto nodes = inspect_ui_markup(source);
    const auto& node = element(nodes, index);
    editable(node);
    require(node.parent != UiMarkupElement::no_parent, "Root elements cannot move");
    std::vector<std::size_t> siblings;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].parent == node.parent)
            siblings.push_back(i);
    const auto found = std::ranges::find(siblings, index);
    const auto position = std::size_t(found - siblings.begin());
    require(direction < 0 ? position > 0 : position + 1 < siblings.size(),
            "This element is already at that end of its container");
    const auto adjacent = siblings[direction < 0 ? position - 1 : position + 1];
    editable(nodes[adjacent]);
    const auto left = nodes[std::min(index, adjacent)].source;
    const auto right = nodes[std::max(index, adjacent)].source;
    const auto view = [&](UiSourceRange range) {
        return source.substr(range.begin, range.end - range.begin);
    };
    const auto replacement = std::string(view(right)) + std::string(view({left.end, right.begin})) +
                             std::string(view(left));
    return replaced(source, {left.begin, right.end}, replacement);
}
} // namespace forge
