#include "ui_markup.hpp"
#include "ui_markup_authoring.hpp"
#include <array>
#include <forge/ui_assets.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

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
              slice(style, declarations[1].value) == " calc(1px + 2px)" &&
              slice(style, declarations[0].source) == "custom-2: 'quoted;:}';" &&
              slice(style, declarations[1].source) == "left: calc(1px + 2px);",
          "Inline ranges split quoted delimiters, functions or property digits");
}
void layout_group_edits() {
    const std::string source =
        "<!-- group -->\n<rml><body><div id='a' style='position:absolute;left:10px;top:20px;"
        "width:30px;height:40px;color:red'>A &amp; B</div><button id='b' "
        "style=\"position:absolute;left:100px;top:200px;width:80px;height:50px;"
        "custom:url('x;y:z')\">Keep</button></body></rml>\n";
    const auto a = find(source, "div"), b = find(source, "button");
    const std::array original{forge::UiMarkupLayoutEdit{a, {10, 20, 30, 40}},
                              forge::UiMarkupLayoutEdit{b, {100, 200, 80, 50}}};
    check(forge::set_ui_markup_layouts(source, original) == source,
          "An unchanged layout group rewrote source bytes");
    const std::array changes{forge::UiMarkupLayoutEdit{b, {120.5f, 21, 85, 50}},
                             forge::UiMarkupLayoutEdit{a, {-10, 21, 30, 45}}};
    const auto expected = forge::set_ui_markup_layout(
        forge::set_ui_markup_layout(source, a, changes[1].pixels), b, changes[0].pixels);
    const auto edited = forge::set_ui_markup_layouts(source, changes);
    check(edited == expected && edited.find("A &amp; B") != edited.npos &&
              edited.find("custom:url('x;y:z')") != edited.npos,
          "Unordered group replacements damaged source ranges or unrelated markup");
    check(forge::set_ui_markup_layouts(edited, original) == source,
          "Repeated group edits accumulated styles or changed unrelated bytes");
    rejects([&] { forge::set_ui_markup_layouts(source, {}); });
    const std::array repeated{original[0], original[0]};
    rejects([&] { forge::set_ui_markup_layouts(source, repeated); });
    auto invalid = changes;
    invalid[1].pixels.width = 0;
    rejects([&] { forge::set_ui_markup_layouts(source, invalid); });
    invalid = changes;
    invalid[1].element = std::numeric_limits<std::size_t>::max();
    rejects([&] { forge::set_ui_markup_layouts(source, invalid); });
    std::vector<forge::UiMarkupLayoutEdit> oversized(forge::ui_source_selection_limit + 1,
                                                     original[0]);
    rejects([&] { forge::set_ui_markup_layouts(source, oversized); });
    check(forge::set_ui_markup_layouts(source, original) == source,
          "A rejected group changed the original admitted source");
}
void layout_group_geometry() {
    using forge::UiMarkupLayoutBox;
    const std::array boxes{UiMarkupLayoutBox{{4, {10, 20, 100, 40}}, {100, 200, 120, 60}},
                           UiMarkupLayoutBox{{7, {210, 70, 50, 80}}, {300, 250, 70, 100}},
                           UiMarkupLayoutBox{{9, {410, 220, 70, 40}}, {500, 400, 90, 60}}};
    const auto check_positions = [&](std::string_view action, const std::array<float, 3>& x,
                                     const std::array<float, 3>& y, float dx = 0, float dy = 0) {
        const auto changes = forge::arrange_ui_markup_layouts(boxes, action, dx, dy);
        check(changes.size() == boxes.size(), "Group geometry dropped an element");
        for (std::size_t i = 0; i < boxes.size(); ++i)
            check(changes[i].element == boxes[i].source.element && changes[i].pixels.left == x[i] &&
                      changes[i].pixels.top == y[i] &&
                      changes[i].pixels.width == boxes[i].source.pixels.width &&
                      changes[i].pixels.height == boxes[i].source.pixels.height,
                  "Group geometry ignored native border bounds, order or source dimensions");
    };
    check_positions("move", {22.5f, 222.5f, 422.5f}, {15, 65, 215}, 12.5f, -5);
    check_positions("align_left", {10, 10, 10}, {20, 70, 220});
    check_positions("align_horizontal_center", {195, 220, 210}, {20, 70, 220});
    check_positions("align_right", {380, 430, 410}, {20, 70, 220});
    check_positions("align_top", {10, 210, 410}, {20, 20, 20});
    check_positions("align_vertical_center", {10, 210, 410}, {120, 100, 120});
    check_positions("align_bottom", {10, 210, 410}, {220, 180, 220});
    check_positions("distribute_horizontal", {10, 235, 410}, {20, 70, 220});
    check_positions("distribute_vertical", {10, 210, 410}, {20, 100, 220});
    const std::array unordered{boxes[2], boxes[0], boxes[1]};
    const auto spaced = forge::arrange_ui_markup_layouts(unordered, "distribute_horizontal");
    check(spaced[0].element == 9 && spaced[0].pixels.left == 410 && spaced[1].element == 4 &&
              spaced[1].pixels.left == 10 && spaced[2].element == 7 && spaced[2].pixels.left == 235,
          "Distribution used selection order instead of native spatial order");
    rejects([&] { forge::arrange_ui_markup_layouts({}, "move"); });
    rejects([&] { forge::arrange_ui_markup_layouts(std::span(boxes).first(1), "move"); });
    rejects([&] {
        forge::arrange_ui_markup_layouts(std::span(boxes).first(2), "distribute_vertical");
    });
    rejects([&] { forge::arrange_ui_markup_layouts(boxes, "other"); });
    rejects([&] { forge::arrange_ui_markup_layouts(boxes, "align_left", 1, 0); });
    rejects([&] { forge::arrange_ui_markup_layouts(boxes, "move", 65536, 0); });
    rejects([&] {
        forge::arrange_ui_markup_layouts(boxes, "move", std::numeric_limits<float>::infinity(), 0);
    });
    auto invalid = boxes;
    invalid[2].bounds.width = 0;
    rejects([&] { forge::arrange_ui_markup_layouts(invalid, "align_left"); });
    invalid = boxes;
    invalid[2].source.element = invalid[0].source.element;
    rejects([&] { forge::arrange_ui_markup_layouts(invalid, "move"); });
    invalid = boxes;
    for (auto& item : invalid)
        item.bounds.left = 0;
    rejects([&] { forge::arrange_ui_markup_layouts(invalid, "distribute_horizontal"); });
}
void group_removals() {
    const std::string source =
        "<!-- keep -->\n<rml><body><div id='a'><p>A</p></div>"
        "<!-- between --><button id='b'>B</button><p>Keep</p></body></rml>\n";
    const auto panel = find(source, "div"), button = find(source, "button");
    const std::array group{button, panel};
    check(forge::remove_ui_markup_elements(source, group) ==
              "<!-- keep -->\n<rml><body><!-- between --><p>Keep</p></body></rml>\n",
          "Unordered group removal damaged retained source/comments or left a subtree");
    check(forge::remove_ui_markup_element(source, panel) ==
              "<!-- keep -->\n<rml><body><!-- between --><button id='b'>B</button>"
              "<p>Keep</p></body></rml>\n",
          "Single source removal changed its original range semantics");
    rejects([&] { forge::remove_ui_markup_elements(source, {}); });
    const std::array duplicates{panel, panel};
    rejects([&] { forge::remove_ui_markup_elements(source, duplicates); });
    const std::array nested{panel, find(source, "p")};
    rejects([&] { forge::remove_ui_markup_elements(source, nested); });
    const std::array protected_group{button, find(source, "body")};
    rejects([&] { forge::remove_ui_markup_elements(source, protected_group); });
    const std::array invalid{button, std::numeric_limits<std::size_t>::max()};
    rejects([&] { forge::remove_ui_markup_elements(source, invalid); });
    const std::vector<std::size_t> oversized(forge::ui_source_selection_limit + 1, button);
    rejects([&] { forge::remove_ui_markup_elements(source, oversized); });
}
void subtree_duplication() {
    const std::string source =
        "<!-- preserve -->\n<rml><body><input id='external'/><div id='panel' class='shared'>"
        "<!-- inside --><label for='entry'>Name &amp; label</label><input id=entry/>"
        "<label for='external'>External</label></div><p>Keep</p></body></rml>\n";
    const auto panel = find(source, "div");
    const auto nodes = forge::inspect_ui_markup(source);
    const auto range = nodes[panel].source;
    const auto candidate = forge::duplicate_ui_markup_element(source, panel, "second");
    const auto original = source.substr(range.begin, range.end - range.begin);
    auto clone = original;
    clone.replace(clone.find("id='panel'"), std::string_view("id='panel'").size(),
                  "id=\"second-panel\"");
    clone.replace(clone.find("id=entry"), std::string_view("id=entry").size(),
                  "id=\"second-entry\"");
    clone.replace(clone.find("for='entry'"), std::string_view("for='entry'").size(),
                  "for=\"second-entry\"");
    check(candidate == source.substr(0, range.end) + clone + source.substr(range.end),
          "Subtree duplication changed old bytes/IDs, lost comments or miswired label targets");
    const auto automatic = forge::duplicate_ui_markup_element(source, panel);
    check(automatic.find("id=\"copy-panel\"") != automatic.npos &&
              automatic.find("id=\"copy-entry\"") != automatic.npos,
          "Automatic duplication did not give the entire subtree new IDs");
    const auto repeated = forge::duplicate_ui_markup_element(automatic, panel);
    check(repeated.find("id=\"copy2-panel\"") != repeated.npos &&
              repeated.find("id=\"copy2-entry\"") != repeated.npos,
          "Repeated duplication reused an existing generated ID");
    const std::string escaped =
        "<rml><body><div id='a&amp;b'><label for='a&amp;b'>Label</label></div></body></rml>";
    const auto escaped_copy = forge::duplicate_ui_markup_element(escaped, find(escaped, "div"));
    check(escaped_copy.find("id=\"copy-a&amp;b\"") != escaped_copy.npos &&
              escaped_copy.find("for=\"copy-a&amp;b\"") != escaped_copy.npos,
          "Copied escaped IDs or internal label targets were double encoded");
    const std::string anonymous = "<rml><body><button class='same'>Go</button></body></rml>";
    check(forge::duplicate_ui_markup_element(anonymous, find(anonymous, "button"), "new") ==
              "<rml><body><button class='same'>Go</button><button class='same' "
              "id=\"new\">Go</button></body></rml>",
          "An anonymous duplicated root did not gain a selectable unique ID");
    rejects([&] { forge::duplicate_ui_markup_element(candidate, panel, "second"); });
    rejects([&] { forge::duplicate_ui_markup_element(source, panel, "Bad prefix"); });
    rejects([&] { forge::duplicate_ui_markup_element(source, find(source, "body")); });
    for (const auto* invalid :
         {"<rml><body><div id='x'/><div id='x'/></body></rml>",
          "<rml><body><div id='x' id='y'/></body></rml>",
          "<rml><body><div id='x'><input id='#self'/></div></body></rml>",
          "<rml><body data-model='live'><div id='x'/></body></rml>",
          "<rml><body><div id='x'><button data-event-click='go'>Go</button></div></body></rml>",
          "<rml><body><div id='x' onclick='go'>Go</div></body></rml>",
          "<rml><body><div id='x'>{{ value }}</div></body></rml>",
          "<rml><body><div id='x'><a href='#x'>Link</a></div></body></rml>",
          "<rml><body><div id='x'><label for='x' for='x'>Label</label></div></body></rml>",
          "<rml><body><div id='x'><style>p{color:red;}</style></div></body></rml>"})
        rejects([&] { forge::duplicate_ui_markup_element(invalid, find(invalid, "div")); });
    const std::string huge =
        "<rml><body><div>" + std::string(140 * 1024, 'x') + "</div></body></rml>";
    check(forge::inspect_ui_markup(huge).size() == 3,
          "Byte-limit fixture is not initially admitted");
    rejects([&] { forge::duplicate_ui_markup_element(huge, find(huge, "div")); });
    std::string crowded = "<rml><body><div>";
    for (int i = 0; i < 4095; ++i)
        crowded += "<br/>";
    crowded += "</div></body></rml>";
    check(forge::inspect_ui_markup(crowded).size() == 4098,
          "Node-limit fixture is not initially admitted");
    rejects([&] { forge::duplicate_ui_markup_element(crowded, find(crowded, "div")); });
    std::string collisions = "<rml><body><div id='panel'/>";
    for (unsigned i = 1; i <= 64; ++i)
        collisions += "<span id='" + (i == 1 ? std::string("copy") : "copy" + std::to_string(i)) +
                      "-panel'/>";
    collisions += "</body></rml>";
    rejects([&] { forge::duplicate_ui_markup_element(collisions, find(collisions, "div")); });
    check(forge::duplicate_ui_markup_element(collisions, find(collisions, "div"), "custom")
                  .find("id=\"custom-panel\"") != std::string::npos,
          "An explicit available prefix failed after the bounded automatic choices");
}
void hierarchy_moves() {
    const std::string source =
        "<!-- outside -->\n<rml><body><div id='left'><button id='a'>A</button>"
        "<!-- gap --><label for='a'>Name</label><input id='c'/></div>"
        "<form id='right'><p id='anchor'>Keep</p></form></body></rml>\n";
    const auto button = find(source, "button"), input = find(source, "input");
    const auto left = find(source, "div"), right = find(source, "form");
    const auto anchor = find(source, "p");
    const std::array group{input, button};
    check(forge::reparent_ui_markup_elements(source, group, right) ==
              "<!-- outside -->\n<rml><body><div id='left'><!-- gap -->"
              "<label for='a'>Name</label></div><form id='right'>"
              "<p id='anchor'>Keep</p><button id='a'>A</button><input id='c'/>"
              "</form></body></rml>\n",
          "Appending moved siblings changed IDs, source order, labels or retained comments");
    check(forge::reparent_ui_markup_elements(source, group, right, anchor) ==
              "<!-- outside -->\n<rml><body><div id='left'><!-- gap -->"
              "<label for='a'>Name</label></div><form id='right'>"
              "<button id='a'>A</button><input id='c'/><p id='anchor'>Keep</p>"
              "</form></body></rml>\n",
          "Before-child group placement used selection order or shifted the insertion range");
    const std::array one{anchor};
    check(forge::reparent_ui_markup_elements(source, one, left, button) ==
              "<!-- outside -->\n<rml><body><div id='left'><p id='anchor'>Keep</p>"
              "<button id='a'>A</button><!-- gap --><label for='a'>Name</label>"
              "<input id='c'/></div><form id='right'></form></body></rml>\n",
          "Moving source backwards damaged its old or new container");
    const std::array input_only{input};
    check(forge::reparent_ui_markup_elements(source, input_only, left, button) ==
              "<!-- outside -->\n<rml><body><div id='left'><input id='c'/>"
              "<button id='a'>A</button><!-- gap --><label for='a'>Name</label>"
              "</div><form id='right'><p id='anchor'>Keep</p></form></body></rml>\n",
          "Same-parent placement lost or duplicated source");
    const std::array already_last{right};
    check(forge::reparent_ui_markup_elements(source, already_last, find(source, "body")) == source,
          "A true hierarchy no-op changed source");
    const std::array moving_parent{left};
    rejects([&] { forge::reparent_ui_markup_elements(source, moving_parent, left); });
    rejects([&] { forge::reparent_ui_markup_elements(source, moving_parent, button); });
    rejects([&] { forge::reparent_ui_markup_elements(source, group, input); });
    rejects([&] { forge::reparent_ui_markup_elements(source, group, right, button); });
    rejects([&] { forge::reparent_ui_markup_elements(source, group, left, input); });
    rejects([&] { forge::reparent_ui_markup_elements(source, {}, right); });
    const std::array duplicate{button, button}, mixed{button, anchor}, nested{left, button};
    rejects([&] { forge::reparent_ui_markup_elements(source, duplicate, right); });
    rejects([&] { forge::reparent_ui_markup_elements(source, mixed, right); });
    rejects([&] { forge::reparent_ui_markup_elements(source, nested, right); });
    const std::array protected_group{find(source, "body")};
    rejects([&] { forge::reparent_ui_markup_elements(source, protected_group, right); });
    rejects([&] {
        forge::reparent_ui_markup_elements(source, group, std::numeric_limits<std::size_t>::max());
    });
    const std::array invalid{button, std::numeric_limits<std::size_t>::max()};
    rejects([&] { forge::reparent_ui_markup_elements(source, invalid, right); });
    const std::vector<std::size_t> oversized(forge::ui_source_selection_limit + 1, button);
    rejects([&] { forge::reparent_ui_markup_elements(source, oversized, right); });
    for (const auto* markup :
         {"<rml><body data-model='live'><div>Go</div><form></form></body></rml>",
          "<rml><body><div>Go</div><form data-model='live'></form></body></rml>",
          "<rml><body><div data-event-click='go'>Go</div><form></form></body></rml>",
          "<rml><body><div id='x'>Go</div><form id='x'></form></body></rml>",
          "<rml><body><div>{{value}}</div><form></form></body></rml>",
          "<rml><body><div><label for='#parent'>Go</label></div><form></form></body></rml>",
          "<rml><body><div><label for='x' for='y'>Go</label></div><form></form></body></rml>",
          "<rml><body><div><a href='#x'>Go</a></div><form></form></body></rml>",
          "<rml><body><div><style>p{color:red;}</style></div><form></form></body></rml>"}) {
        const std::string text(markup);
        const std::array moving{find(text, "div")};
        rejects([&] { forge::reparent_ui_markup_elements(text, moving, find(text, "form")); });
    }
    std::string deep = "<rml><body>";
    for (int i = 0; i < 16; ++i)
        deep += "<span>";
    deep += "<div></div>";
    for (int i = 0; i < 16; ++i)
        deep += "</span>";
    deep += "<button>";
    for (int i = 0; i < 15; ++i)
        deep += "<span>";
    deep += "Move";
    for (int i = 0; i < 15; ++i)
        deep += "</span>";
    deep += "</button></body></rml>";
    forge::validate_ui_text(deep, true);
    const std::array moving{find(deep, "button")};
    rejects([&] { forge::reparent_ui_markup_elements(deep, moving, find(deep, "div")); });
}
void inline_properties() {
    const std::string source =
        "<!-- retained -->\n<rml><body data-model='demo'><button id='x' data-event-click='go' "
        "style=\" /* lead */ color : red ; /* between */width: calc(10px + 2px);\">"
        "Go</button></body></rml>\n";
    const auto index = find(source, "button");
    const auto nodes = forge::inspect_ui_markup(source);
    check(forge::read_ui_markup_style(source, nodes, index, "color") == "red" &&
              !forge::read_ui_markup_style(source, nodes, index, "height"),
          "Inline property reading confused authored/default values or whitespace");
    auto expected = source;
    expected.replace(expected.find(" red "), 5, " #123456 ");
    check(forge::set_ui_markup_style(source, index, "color", "#123456") == expected,
          "Property replacement damaged unrelated CSS, bindings, IDs or comments");
    check(forge::set_ui_markup_style(source, index, "color", "red") == source,
          "An identical inline value changed source formatting");
    expected = source;
    expected.insert(expected.find("\">Go"), "height:20px;");
    check(forge::set_ui_markup_style(source, index, "height", "20px") == expected,
          "Adding a property rewrote the old declarations or lost its separator");
    expected = source;
    expected.erase(expected.find("color : red ;"), std::string_view("color : red ;").size());
    check(forge::set_ui_markup_style(source, index, "color", {}) == expected &&
              forge::set_ui_markup_style(source, index, "height", {}) == source,
          "Property removal damaged surrounding source or changed a missing override");
    const std::string no_style = "<rml><body><p>Text</p></body></rml>";
    check(forge::set_ui_markup_style(no_style, find(no_style, "p"), "color", "red") ==
              "<rml><body><p style=\"color:red;\">Text</p></body></rml>",
          "An anonymous widget did not receive one quoted inline declaration");
    check(forge::set_ui_markup_style(no_style, find(no_style, "p"), "font-family", "\"Lato\"") ==
              "<rml><body><p style='font-family:\"Lato\";'>Text</p></body></rml>",
          "New style attributes did not choose an unambiguous quote form");
    const std::string void_node = "<rml><body><input id=x/></body></rml>";
    check(forge::set_ui_markup_style(void_node, find(void_node, "input"), "width", "20px") ==
              "<rml><body><input id=x style=\"width:20px;\"/></body></rml>",
          "Adding an inline style damaged a void tag or unquoted unrelated ID");
    const std::string unterminated =
        "<rml><body><p style='width:10px /* tail */'>Text</p></body></rml>";
    check(forge::set_ui_markup_style(unterminated, find(unterminated, "p"), "height", "20px") ==
              "<rml><body><p style='width:10px /* tail */;height:20px;'>Text</p></body></rml>",
          "An unterminated existing declaration swallowed a new property");
    const std::string comment_only = "<rml><body><p style='/* keep */ '>Text</p></body></rml>";
    check(forge::set_ui_markup_style(comment_only, find(comment_only, "p"), "color", "red") ==
              "<rml><body><p style='/* keep */ color:red;'>Text</p></body></rml>",
          "Appending to comment-only style lost its comments");
    const std::string only = "<rml><body><p style='color:red'>Text</p></body></rml>";
    check(forge::set_ui_markup_style(only, find(only, "p"), "color", {}) ==
              "<rml><body><p style=''>Text</p></body></rml>",
          "Removing an unterminated sole declaration removed unrelated attribute bytes");
    for (const auto* value :
         {"red; width:4px", "red;", "", "   ", "{{color}}", "a&b", "a<b", "rgb(1,2", "999999999px"})
        rejects([&] { forge::set_ui_markup_style(source, index, "color", value); });
    rejects([&] { forge::set_ui_markup_style(source, index, "color", std::string(1, '\0')); });
    rejects([&] { forge::set_ui_markup_style(source, index, "color", std::string(8193, 'a')); });
    rejects([&] { forge::set_ui_markup_style(source, index, "font-size", "257px"); });
    rejects([&] { forge::set_ui_markup_style(source, index, "font-family", "\"Lato\""); });
    for (const auto* property : {"", "Color", "9color", "bad name"})
        rejects([&] { forge::set_ui_markup_style(source, index, property, "red"); });
    rejects([&] { forge::set_ui_markup_style(source, index, std::string(65, 'a'), "red"); });
    rejects([&] { forge::set_ui_markup_style(source, find(source, "body"), "color", "red"); });
    rejects([&] {
        forge::set_ui_markup_style(source, std::numeric_limits<std::size_t>::max(), "color", "red");
    });
    for (const auto* markup :
         {"<rml><body><p style=color:red;>Text</p></body></rml>",
          "<rml><body><p style='co lor:red;'>Text</p></body></rml>",
          "<rml><body><p style='color:r&#101;d;'>Text</p></body></rml>",
          "<rml><body><p style='color:red;' style='color:blue;'>Text</p></body></rml>",
          "<rml><body><p style='color:red;COLOR:blue;'>Text</p></body></rml>"}) {
        const std::string text(markup);
        rejects([&] { forge::set_ui_markup_style(text, find(text, "p"), "color", "green"); });
    }
    const std::string tag_limit =
        "<rml><body><p data-note='" + std::string(8100, 'x') + "'>Text</p></body></rml>";
    forge::validate_ui_text(tag_limit, true);
    rejects([&] {
        forge::set_ui_markup_style(tag_limit, find(tag_limit, "p"), "font-family",
                                   "'" + std::string(200, 'x') + "'");
    });
}
} // namespace
int main() {
    try {
        ranges();
        invalid();
        edits();
        layout_edits();
        layout_group_edits();
        layout_group_geometry();
        group_removals();
        subtree_duplication();
        hierarchy_moves();
        inline_properties();
        std::cout << "Source-preserving outline/attributes and shared validator errors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
