#include "ui_markup_authoring.hpp"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <forge/ui_assets.hpp>
#include <limits>
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
std::string replaced(std::string_view source, UiSourceRange range, std::string_view value) {
    std::string result(source);
    result.replace(range.begin, range.end - range.begin, value);
    validate_ui_text(result, true);
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
    const UiMarkupAttribute* style = nullptr;
    for (const auto& attr : node.attributes)
        if (attr.name == "style") {
            require(!style && attr.has_value && attr.quote,
                    "Use one quoted inline style for canvas layout edits");
            style = &attr;
        }
    require(style, "Define the absolute pixel layout in the element's inline style");
    const auto inline_style =
        source.substr(style->value.begin, style->value.end - style->value.begin);
    require(inline_style.find('&') == inline_style.npos,
            "Entity-encoded inline styles use Source for layout edits");
    const auto declarations = inspect_ui_style(inline_style);
    const auto value_range = [&](std::string_view name) {
        const UiStyleDeclaration* found = nullptr;
        for (const auto& declaration : declarations)
            if (declaration.name == name) {
                require(!found, "Resolve duplicate layout properties in Source first");
                found = &declaration;
            }
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
        result.values[i] = {style->value.begin + range.begin, style->value.begin + range.end};
    }
    valid_layout(result.pixels);
    return result;
}
} // namespace
std::vector<UiMarkupLayoutEdit> arrange_ui_markup_layouts(std::span<const UiMarkupLayoutBox> boxes,
                                                          std::string_view action, float dx,
                                                          float dy) {
    require(boxes.size() >= 2 && boxes.size() <= ui_layout_selection_limit,
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
    require(!changes.empty() && changes.size() <= ui_layout_selection_limit,
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
    if (edits.empty())
        return std::string(source);
    std::ranges::sort(edits,
                      [](const auto& a, const auto& b) { return a.first.begin > b.first.begin; });
    std::string result(source);
    std::size_t next_begin = source.size();
    for (const auto& [range, value] : edits) {
        require(range.end <= next_begin, "Canvas layout replacements overlap");
        next_begin = range.begin;
        result.replace(range.begin, range.end - range.begin, value);
    }
    validate_ui_text(result, true);
    return result;
}
std::string set_ui_markup_attribute(std::string_view source, std::size_t index,
                                    std::string_view name, std::optional<std::string> value) {
    identifier(name);
    const auto nodes = inspect_ui_markup(source);
    const auto& node = element(nodes, index);
    require(node.tag != "head" && node.tag != "style" && node.tag != "link" && node.tag != "meta",
            "Edit resource and style attributes in the source view");
    const UiMarkupAttribute* found = nullptr;
    for (const auto& attribute : node.attributes)
        if (attribute.name == name) {
            require(!found, "Resolve duplicate attributes in the source view first");
            found = &attribute;
        }
    if (!value)
        return found ? replaced(source, found->source, {}) : std::string(source);
    const auto assignment = std::string(name) + "=\"" + escaped(*value) + '"';
    if (found)
        return replaced(source, found->source, assignment);
    auto insertion = node.opening.end - 1;
    if (source[insertion - 1] == '/')
        --insertion;
    return replaced(source, {insertion, insertion}, " " + assignment);
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
    const auto nodes = inspect_ui_markup(source);
    const auto& node = element(nodes, index);
    editable(node);
    return replaced(source, node.source, {});
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
