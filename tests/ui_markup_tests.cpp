#include "ui_markup.hpp"
#include "ui_markup_authoring.hpp"
#include <array>
#include <forge/ui_assets.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string_view slice(std::string_view text, forge::UiSourceRange range) {
    check(range.begin <= range.end && range.end <= text.size(),
          "Source range escaped its snapshot");
    return text.substr(range.begin, range.end - range.begin);
}
void ranges() {
    const std::string text =
        "<!-- untouched > comment -->\n<rml><head><style>/* <fake> */ body { color: red; }"
        "</style></head><body><div id='panel' data-title=\"a > b &amp; c\" hidden>"
        "Text &lt; label<br/><input id=field/></div></body></rml>\n";
    forge::validate_ui_text(text, true);
    const auto elements = forge::inspect_ui_markup(text);
    check(elements.size() == 7, "Comments/style text became elements");
    const std::array names{"rml", "head", "style", "body", "div", "br", "input"};
    const std::array parents{forge::UiMarkupElement::no_parent,
                             std::size_t(0),
                             std::size_t(1),
                             std::size_t(0),
                             std::size_t(3),
                             std::size_t(4),
                             std::size_t(4)};
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& element = elements[i];
        check(element.tag == names[i] && element.parent == parents[i], "Incorrect source outline");
        check(slice(text, element.opening).starts_with("<"), "Opening range lost its delimiter");
        check(element.source.begin == element.opening.begin &&
                  element.source.end == element.closing.end &&
                  element.opening.end == element.content.begin &&
                  element.content.end == element.closing.begin,
              "Source ranges do not partition an element");
        if (element.self_closing)
            check(element.content.begin == element.source.end &&
                      slice(text, element.closing).empty(),
                  "Void element acquired content or closing bytes");
        else
            check(slice(text, element.closing).starts_with("</"), "Missing closing source range");
    }
    check(slice(text, elements[2].content) == "/* <fake> */ body { color: red; }",
          "Style source was reparsed or normalized");
    const auto& attributes = elements[4].attributes;
    check(attributes.size() == 3 && attributes[0].name == "id" &&
              slice(text, attributes[0].source) == "id='panel'" &&
              slice(text, attributes[0].value) == "panel" && attributes[0].quote == '\'' &&
              attributes[1].name == "data-title" &&
              slice(text, attributes[1].value) == "a > b &amp; c" && attributes[1].quote == '"' &&
              attributes[2].name == "hidden" && !attributes[2].has_value &&
              slice(text, attributes[2].source) == "hidden",
          "Attribute ranges changed quoted, entity or Boolean intent");
    check(slice(text, elements[6].attributes.at(0).source) == "id=field" &&
              slice(text, elements[6].attributes.at(0).value) == "field",
          "Unquoted attribute consumed the self-closing delimiter");
    const auto content = slice(text, elements[4].content);
    check(content == "Text &lt; label<br/><input id=field/>", "Content was serialized or altered");
}
void invalid() {
    for (const auto* text :
         {"<rml><body><div></body></rml>", "<rml><body><script>bad</script></body></rml>",
          "<rml><body><div id='x></div></body></rml>"}) {
        std::string validation_error;
        try {
            forge::validate_ui_text(text, true);
        } catch (const std::exception& error) {
            validation_error = error.what();
        }
        check(!validation_error.empty(), "Invalid fixture unexpectedly admitted");
        bool rejected = false;
        try {
            (void)forge::inspect_ui_markup(text);
        } catch (const std::exception& error) {
            check(validation_error == error.what(),
                  "Outline introduced another validation grammar");
            rejected = true;
        }
        check(rejected, "Incomplete markup produced an editable outline");
    }
}
template <class F> void rejects(F&& operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid structured source edit was accepted");
}
void edits() {
    const std::string source =
        "<!-- keep -->\r\n<rml><head><style>/* retain */ body { color: red; }</style>"
        "</head><body id='root'>\r\n  <p id='first'>Old &amp; text</p><!-- between -->"
        "<button class='bright'>Go</button><input id=entry/>\r\n</body></rml>\r\n";
    const auto find = [](const std::string& text, std::string_view tag) {
        const auto nodes = forge::inspect_ui_markup(text);
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].tag == tag)
                return i;
        throw std::runtime_error("Fixture element missing");
    };
    auto expected = source;
    const auto old = expected.find("Old &amp; text");
    expected.replace(old, std::string("Old &amp; text").size(), "New &amp; &lt;literal&gt;");
    const auto text = forge::set_ui_markup_text(source, find(source, "p"), "New & <literal>");
    check(text == expected, "Text edit rewrote unrelated bytes/CRLF/comments/styles");
    check(forge::set_ui_markup_text(source, find(source, "p"), "Tick {{tick}} & paused {{paused}}")
                  .find("Tick {{tick}} &amp; paused {{paused}}") != std::string::npos,
          "Text authoring changed RML model binding semantics");
    const auto input = find(source, "input");
    auto attribute = forge::set_ui_markup_attribute(source, input, "id", "a&\"'<b>");
    check(attribute.find("<input id=\"a&amp;&quot;'&lt;b&gt;\"/>") != attribute.npos,
          "Attribute edit changed literal meaning or self-closing delimiter");
    attribute = forge::set_ui_markup_attribute(source, input, "disabled", "true");
    check(attribute.find("<input id=entry disabled=\"true\"/>") != attribute.npos,
          "Attribute insertion damaged existing unquoted source");
    attribute = forge::set_ui_markup_attribute(source, input, "id", {});
    check(attribute.find("<input />") != attribute.npos, "Attribute removal removed delimiter");
    check(forge::set_ui_markup_attribute(source, input, "missing", {}) == source,
          "Absent attribute removal changed the source");
    const auto body = find(source, "body"), button = find(source, "button");
    const auto inserted =
        forge::insert_ui_markup_child(source, body, "span", "Name & value", button);
    check(inserted.find("<!-- between --><span>Name &amp; value</span><button") != inserted.npos,
          "Child insertion lost source order or comments");
    const auto moved = forge::move_ui_markup_element(source, button, -1);
    check(moved.find("<button class='bright'>Go</button><!-- between --><p id='first'>") !=
              moved.npos,
          "Sibling movement lost attributes/content or intervening source");
    check(forge::move_ui_markup_element(moved, find(moved, "button"), 1) == source,
          "Inverse sibling movement did not restore exact source bytes");
    check(forge::remove_ui_markup_element(inserted, find(inserted, "span")) == source,
          "Remove inserted element did not preserve original source");
    rejects([&] { forge::set_ui_markup_text(source, body, "erase children"); });
    rejects([&] { forge::set_ui_markup_text(source, input, "text"); });
    rejects([&] { forge::remove_ui_markup_element(source, body); });
    rejects([&] { forge::move_ui_markup_element(source, button, 0); });
    rejects([&] { forge::move_ui_markup_element(source, find(source, "p"), -1); });
    rejects([&] { forge::insert_ui_markup_child(source, body, "script", "code"); });
    rejects([&] { forge::insert_ui_markup_child(source, input, "span", "child"); });
    rejects([&] { forge::insert_ui_markup_child(source, body, "span", {}, body); });
    rejects([&] { forge::set_ui_markup_attribute(source, input, "bad name", "x"); });
    rejects([&] { forge::set_ui_markup_attribute(source, body, "style", "font-size:999999px;"); });
    const std::string duplicate = "<rml><body><p id='a' id='b'>Text</p></body></rml>";
    rejects([&] { forge::set_ui_markup_attribute(duplicate, 2, "id", "new"); });
    const std::string comment = "<rml><body><p>Text<!-- keep --></p></body></rml>";
    rejects([&] { forge::set_ui_markup_text(comment, 2, "new"); });
}
} // namespace
int main() {
    try {
        ranges();
        invalid();
        edits();
        std::cout << "Source-preserving outline/attributes and shared validator errors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
