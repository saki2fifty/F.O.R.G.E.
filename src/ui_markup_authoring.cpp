#include "ui_markup_authoring.hpp"
#include <RmlUi/Core/StringUtilities.h>
#include <algorithm>
#include <forge/ui_assets.hpp>
#include <stdexcept>

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
const UiMarkupElement& element(const std::vector<UiMarkupElement>& nodes, std::size_t index) {
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
} // namespace
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
