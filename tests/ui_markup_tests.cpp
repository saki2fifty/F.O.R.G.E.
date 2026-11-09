#include "ui_markup.hpp"
#include "ui_markup_authoring.hpp"
#include <array>
#include <forge/ui_assets.hpp>
#include <iostream>
#include <limits>
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
std::size_t find(const std::string& text, std::string_view tag) {
    const auto nodes = forge::inspect_ui_markup(text);
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].tag == tag)
            return i;
    throw std::runtime_error("Fixture element missing");
}
void edits() {
    const std::string source =
        "<!-- keep -->\r\n<rml><head><style>/* retain */ body { color: red; }</style>"
        "</head><body id='root'>\r\n  <p id='first'>Old &amp; text</p><!-- between -->"
        "<button class='bright'>Go</button><input id=entry/>\r\n</body></rml>\r\n";
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
void layout_edits() {
    const std::string source = "<!-- keep -->\n<rml><body><div id='panel' class='bright' style=\""
                               "position: absolute; /* keep ; : */ color: rgb(1,2,3); top: 30px ; "
                               "custom: url('a;b:c'); LEFT: 20px; height:60px; width:100px\">"
                               "Text &amp; content</div></body></rml>\n";
    const auto index = find(source, "div");
    const forge::UiPixelLayout original{20, 30, 100, 60}, updated{-12.5f, 0, 210.25f, 90};
    check(forge::read_ui_markup_layout(source, index) == original,
          "Inline pixel properties lost source order, case or comments");
    check(forge::set_ui_markup_layout(source, index, original) == source,
          "No-op layout edit changed original bytes");
    const auto edited = forge::set_ui_markup_layout(source, index, updated);
    auto expected = source;
    for (const auto& [before, after] :
         std::array{std::pair{"30px", "0px"}, std::pair{"20px", "-12.5px"},
                    std::pair{"60px", "90px"}, std::pair{"100px", "210.25px"}})
        expected.replace(expected.find(before), std::string_view(before).size(), after);
    check(edited == expected && forge::read_ui_markup_layout(edited, index) == updated,
          "Atomic layout edit changed unrelated source or values");
    const auto repeated = forge::set_ui_markup_layout(edited, index, {0, 1, 2, 3});
    const auto restored = forge::set_ui_markup_layout(repeated, index, original);
    check(restored == source, "Repeated edits accumulated declarations or changed source");
    for (const auto* style : {"position:relative;left:0px;top:0px;width:10px;height:10px",
                              "position:absolute;left:0%;top:0px;width:10px;height:10px",
                              "position:absolute;left:0px;top:0px;width:auto;height:10px",
                              "position:absolute;left:0px;top:0px;width:10px;height:10px;left:2px",
                              "position:absolute;left:/*keep*/0px;top:0px;width:10px;height:10px",
                              "position:absolute;left:0px;top:0px;width:10px;height:10px;&amp;",
                              "position:absolute;left:0px;top:0px;width:0px;height:10px"}) {
        const auto invalid =
            "<rml><body><div style=\"" + std::string(style) + "\"></div></body></rml>";
        rejects([&] { forge::read_ui_markup_layout(invalid, 2); });
    }
    const auto bound =
        forge::set_ui_markup_attribute(source, find(source, "body"), "data-if", "ok");
    rejects([&] { forge::read_ui_markup_layout(bound, find(bound, "div")); });
    rejects([&] { forge::set_ui_markup_layout(source, index, {0, 0, -1, 2}); });
    rejects([&] { forge::set_ui_markup_layout(source, index, {65537, 0, 1, 1}); });
    rejects([&] {
        forge::set_ui_markup_layout(source, index,
                                    {std::numeric_limits<float>::infinity(), 0, 1, 1});
    });
    rejects([&] { forge::inspect_ui_style("body {left:1px}"); });
    const std::string style = "/* first */ custom-2: 'quoted;:}';left: calc(1px + 2px);";
    const auto declarations = forge::inspect_ui_style(style);
    check(declarations.size() == 2 && declarations[0].name == "custom-2" &&
              slice(style, declarations[0].value) == " 'quoted;:}'" &&
              slice(style, declarations[1].value) == " calc(1px + 2px)",
          "Inline ranges split quoted delimiters, functions or property digits");
}
} // namespace
int main() {
    try {
        ranges();
        invalid();
        edits();
        layout_edits();
        std::cout << "Source-preserving outline/attributes and shared validator errors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
