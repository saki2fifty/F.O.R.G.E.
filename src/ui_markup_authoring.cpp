#include "ui_markup_authoring.hpp"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
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
std::pair<UiSourceRange, std::string> style_edit(std::string_view text,
                                                 std::span<const UiStyleDeclaration> declarations,
                                                 std::string_view property,
                                                 const std::optional<std::string>& value) {
    identifier(property);
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
std::vector<UiMarkupLayoutEdit> arrange_ui_markup_layouts(std::span<const UiMarkupLayoutBox> boxes,
                                                          std::string_view action, float dx,
                                                          float dy) {
    require(boxes.size() >= 2 && boxes.size() <= ui_source_selection_limit,
            "Choose2..32 elements for a group layout operation");
    require(std::ranges::any_of(ui_layout_group_actions,
                                [&](const auto& item) { return item.first == action; }),
            "Unknown group layout operation");
    require(std::isfinite(dx) && std::isfinite(dy), "Group movement must use finite pixels");
    require(action == "move" || (dx == 0 && dy == 0),
            "Only group movement accepts horizontal/vertical offsets");
    double left = std::numeric_limits<double>::max(), top = left;
    double right = std::numeric_limits<double>::lowest(), bottom = right;
    std::vector<UiMarkupLayoutEdit> result;
    result.reserve(boxes.size());
    for (const auto& box : boxes) {
        valid_layout(box.source.pixels);
        for (const auto value :
             {box.bounds.left, box.bounds.top, box.bounds.width, box.bounds.height})
            require(std::isfinite(value), "Native group bounds must be finite");
        require(box.bounds.width > 0 && box.bounds.height > 0,
                "Native group bounds must have positive sizes");
        require(std::ranges::none_of(
                    result, [&](const auto& item) { return item.element == box.source.element; }),
                "A layout group cannot repeat an element");
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
std::string remove_ui_stylesheet_rule(std::string_view source, std::size_t index) {
    const auto rules = inspect_ui_stylesheet(source);
    return replaced(source, stylesheet_rule(rules, index).source, {}, false);
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
                                             std::optional<std::size_t> before) {
    const auto found = std::ranges::find(ui_markup_control_kinds, kind, &UiMarkupControlKind::id);
    require(found != ui_markup_control_kinds.end(), "Choose a supported UI control kind");
    require(literal.size() <= 8192, "Initial UI text/value is limited to 8192 bytes");
    require(found->accepts_text() || literal.empty(), "This control has no initial text/value");
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
    return {std::move(candidate), index};
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
