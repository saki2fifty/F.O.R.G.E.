#include "ui_markup.hpp"
#include "ui_markup_authoring.hpp"
#include <algorithm>
#include <array>
#include <cmath>
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
    auto sizing = boxes;
    sizing[1].bounds.width = 82;
    sizing[1].bounds.height = 109;
    for (const std::string_view action : {"match_width", "match_height", "match_size"}) {
        const auto changes = forge::arrange_ui_markup_layouts(sizing, action);
        const std::array widths{70.f, 58.f, 70.f}, heights{40.f, 31.f, 40.f};
        for (std::size_t i = 0; i < changes.size(); ++i)
            check(changes[i].pixels.left == sizing[i].source.pixels.left &&
                      changes[i].pixels.top == sizing[i].source.pixels.top &&
                      changes[i].pixels.width ==
                          (action == "match_height" ? sizing[i].source.pixels.width : widths[i]) &&
                      changes[i].pixels.height ==
                          (action == "match_width" ? sizing[i].source.pixels.height : heights[i]),
                  "Matching copied source sizes instead of native dimensions or moved a member");
    }
    const std::array reversed{sizing[2], sizing[1], sizing[0]};
    const auto matched = forge::arrange_ui_markup_layouts(reversed, "match_size");
    check(matched[0].pixels.width == 100 && matched[0].pixels.height == 40 &&
              matched[1].pixels.width == 88 && matched[1].pixels.height == 31 &&
              matched[2] == sizing[0].source,
          "Group matching ignored the last requested reference member");
    auto too_small = sizing;
    too_small.back().bounds.width = 1;
    rejects([&] { forge::arrange_ui_markup_layouts(too_small, "match_width"); });
    auto too_large = sizing;
    too_large.back().bounds.height = 70000;
    rejects([&] { forge::arrange_ui_markup_layouts(too_large, "match_height"); });
    rejects([&] { forge::arrange_ui_markup_layouts(sizing, "match_size", 1, 0); });
    const auto snapped = forge::arrange_ui_markup_layouts(boxes, "snap_grid");
    check(snapped[0].pixels.left == 14 && snapped[0].pixels.top == 20 &&
              snapped[1].pixels.left == 214 && snapped[1].pixels.top == 68 &&
              snapped[2].pixels.left == 414 && snapped[2].pixels.top == 220,
          "Grid alignment ignored native corners or source coordinate offsets");
    auto negative = boxes[0];
    negative.bounds.left = -12;
    negative.bounds.top = -4;
    const std::array singleton{negative};
    const auto rounded = forge::arrange_ui_markup_layouts(singleton, "snap_grid", 0, 0, 8);
    check(rounded[0].pixels.left == 6 && rounded[0].pixels.top == 16 &&
              rounded[0].pixels.width == negative.source.pixels.width &&
              rounded[0].pixels.height == negative.source.pixels.height,
          "Grid ties did not round away from zero or changed dimensions");
    rejects([&] { forge::arrange_ui_markup_layouts(boxes, "snap_grid", 0, 0, 0); });
    rejects([&] { forge::arrange_ui_markup_layouts(boxes, "snap_grid", 0, 0, 1025); });
    rejects([&] {
        forge::arrange_ui_markup_layouts(boxes, "snap_grid", 0, 0,
                                         std::numeric_limits<float>::quiet_NaN());
    });
    rejects([&] { forge::arrange_ui_markup_layouts(boxes, "align_left", 0, 0, 16); });
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
void layout_transform_geometry() {
    using forge::UiMarkupLayoutBox;
    const std::array boxes{UiMarkupLayoutBox{{4, {10, 20, 100, 40}}, {100, 200, 120, 60}},
                           UiMarkupLayoutBox{{7, {210, 70, 50, 80}}, {300, 250, 70, 100}}};
    const auto apply = [&](std::string_view action, float dx, float dy,
                           std::optional<float> grid = {}) {
        return forge::transform_ui_markup_layouts(boxes, action, dx, dy, grid);
    };
    const auto moved = apply("move", 5, -7);
    check(moved[0].source.pixels.left == 15 && moved[0].source.pixels.top == 13 &&
              moved[1].bounds.left == 305 && moved[1].bounds.top == 243 &&
              moved[0].source.pixels.width == 100 && moved[1].source.pixels.height == 80,
          "Transform movement changed sizes or ignored source/native offsets");
    const auto snapped = apply("move", 5, -7, 8);
    check(snapped[0].source.pixels.left == 14 && snapped[0].source.pixels.top == 10 &&
              snapped[1].bounds.left == 304 && snapped[1].bounds.top == 240 &&
              snapped[1].bounds.left - snapped[0].bounds.left == 200 &&
              snapped[1].bounds.top - snapped[0].bounds.top == 50,
          "Drag grid did not use the active anchor or preserve group spacing");
    const auto axis = apply("move_axis", 3, 12, 8);
    check(axis[0].source.pixels.left == 10 && axis[0].source.pixels.top == 34 &&
              axis[1].bounds.left == 300 && axis[1].bounds.top == 264,
          "Axis constraint snapped the inactive off-grid axis");
    const auto tie = apply("move_axis", 5, 5);
    check(tie[0].source.pixels.left == 15 && tie[0].source.pixels.top == 20,
          "Movement-axis tie did not prefer horizontal");
    const auto zero = apply("move", 0, 0, 8);
    check(zero[0].source == boxes[0].source && zero[1].source == boxes[1].source,
          "Clicking without a drag snapped an off-grid group");
    const auto clamped = apply("move", 1000000, 0);
    check(clamped[0].source.pixels.left == 65336 && clamped[1].source.pixels.left == 65536,
          "Group drag clamp did not preserve spacing at source bounds");
    const std::array one{boxes.front()};
    const auto aspect = forge::transform_ui_markup_layouts(one, "resize_aspect", 24, 0);
    check(aspect[0].source.pixels.width == 124 && aspect[0].source.pixels.height == 52 &&
              aspect[0].bounds.width == 144 && aspect[0].bounds.height == 72 &&
              aspect[0].bounds.left == 100 && aspect[0].bounds.top == 200,
          "Aspect resize ignored native borders/padding or moved its corner");
    auto asymmetric = one;
    asymmetric[0].bounds.height = 70;
    const auto aspect_grid =
        forge::transform_ui_markup_layouts(asymmetric, "resize_aspect", 20, 0, 8);
    check(aspect_grid[0].source.pixels.width == 124 && aspect_grid[0].source.pixels.height == 54 &&
              aspect_grid[0].bounds.width == 144 && aspect_grid[0].bounds.height == 84,
          "Grid did not snap the dominant size while retaining visible aspect");
    const auto resize_grid = forge::transform_ui_markup_layouts(one, "resize", 5, 1, 8);
    check(resize_grid[0].source.pixels.width == 108 && resize_grid[0].source.pixels.height == 44 &&
              resize_grid[0].bounds.width == 128 && resize_grid[0].bounds.height == 64,
          "Resize grid used CSS sizes instead of native border sizes");
    const auto minimum =
        forge::transform_ui_markup_layouts(one, "resize_aspect", -1000000, -1000000);
    check(std::abs(minimum[0].source.pixels.width - 22) < .001f &&
              minimum[0].source.pixels.height == 1,
          "Aspect contraction lost its ratio or positive source dimensions");
    auto edge = one;
    edge[0].source.pixels.left = 65535;
    rejects([&] { forge::transform_ui_markup_layouts(edge, "move", 1, 0, 8); });
    rejects([&] { apply("resize", 1, 1); });
    rejects([&] { apply("resize_aspect", 1, 1); });
    rejects([&] { apply("other", 1, 1); });
    rejects([&] { apply("move", std::numeric_limits<float>::infinity(), 0); });
    rejects([&] { apply("move", 1, 1, 0); });
    auto duplicate = boxes;
    duplicate[1].source.element = duplicate[0].source.element;
    rejects([&] { forge::transform_ui_markup_layouts(duplicate, "move", 1, 1); });
    rejects([&] { forge::transform_ui_markup_layouts({}, "move", 1, 1); });
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
void stylesheet_rules() {
    const std::string source = "/* lead */\r\n.a:hover, #b { /* body */ color : red ; "
                               "font-family: 'quoted;{}'; width:calc(10px + 2px) } /* gap */\r\n"
                               "@media (min-width:20px) { .nested { color:blue; } }\r\n"
                               "@decorator shine: linear-gradient { direction:vertical; }\r\n"
                               ".last { color:green; } /* tail */";
    const auto rules = forge::inspect_ui_stylesheet(source);
    check(rules.size() == 4 && rules[0].flat && !rules[1].flat && !rules[2].flat && rules[3].flat &&
              slice(source, rules[0].selector) == ".a:hover, #b " &&
              slice(source, rules[3].source) == ".last { color:green; }",
          "Stylesheet ranges confused comments, strings, pseudo selectors or nested/at-rules");
    check(forge::read_ui_stylesheet_property(source, rules, 0, "color") == "red" &&
              !forge::read_ui_stylesheet_property(source, rules, 0, "height"),
          "Stylesheet property lookup lost authored/absent values");
    auto expected = source;
    expected.replace(expected.find(" red "), 5, " #123456 ");
    check(forge::set_ui_stylesheet_property(source, 0, "color", "#123456") == expected &&
              forge::set_ui_stylesheet_property(source, 0, "color", "red") == source,
          "Stylesheet value edit rewrote unrelated declarations/rules/comments");
    expected = source;
    expected.erase(expected.find("color : red ;"), std::string_view("color : red ;").size());
    check(forge::set_ui_stylesheet_property(source, 0, "color", {}) == expected &&
              forge::set_ui_stylesheet_property(source, 0, "absent", {}) == source,
          "Stylesheet property removal lost source or changed an absent property");
    expected = source;
    expected.insert(expected.find("} /* gap */"), ";height:20px;");
    check(forge::set_ui_stylesheet_property(source, 0, "height", "20px") == expected,
          "Stylesheet append lost an unterminated declaration separator");
    check(forge::set_ui_stylesheet_property(".x{}", 0, "font-family", "\"Lato\"") ==
              ".x{font-family:\"Lato\";}",
          "Stylesheet values incorrectly inherited inline attribute quote restrictions");
    expected = source;
    expected.replace(expected.find(".a:hover, #b "), std::string_view(".a:hover, #b ").size(),
                     ".new > button");
    check(forge::set_ui_stylesheet_selector(source, 0, ".new > button") == expected,
          "Selector edit changed the rule body or adjacent comments");
    expected = source;
    expected.erase(expected.find(".last { color:green; }"),
                   std::string_view(".last { color:green; }").size());
    check(forge::remove_ui_stylesheet_rule(source, 3) == expected &&
              forge::insert_ui_stylesheet_rule(source, ".added") == source + "\n.added {}\n" &&
              forge::insert_ui_stylesheet_rule("", ".first") == ".first {}\n",
          "Rule insertion/removal changed unrelated source or its final separator");
    for (const auto index : {1u, 2u, 4u}) {
        rejects([&] { forge::read_ui_stylesheet_property(source, rules, index, "color"); });
        rejects([&] { forge::set_ui_stylesheet_property(source, index, "color", "blue"); });
        rejects([&] { forge::remove_ui_stylesheet_rule(source, index); });
        rejects([&] { forge::set_ui_stylesheet_selector(source, index, ".renamed"); });
    }
    for (const auto* selector : {"", "@font-face", ".x{} .injected", ".x; .bad", "/* comment */ .x",
                                 ".x\\31", "[name='unterminated]", "<x>"})
        rejects([&] { forge::insert_ui_stylesheet_rule(source, selector); });
    rejects([&] { forge::insert_ui_stylesheet_rule(source, std::string(1025, 'a')); });
    rejects([&] { forge::set_ui_stylesheet_property(source, 0, "color", "red;opacity:0"); });
    rejects([&] { forge::set_ui_stylesheet_property(source, 0, "font-size", "257px"); });
    rejects([&] {
        forge::set_ui_stylesheet_property(".a{color:red;COLOR:blue}", 0, "color", "green");
    });
    rejects([&] { forge::inspect_ui_stylesheet(".a{color:red"); });
    rejects([&] { forge::inspect_ui_stylesheet(".a{} .incomplete"); });
    check(!forge::inspect_ui_stylesheet(".outer { .inner {color:red;} }").front().flat &&
              forge::inspect_ui_stylesheet("[name='{};'] {color:red;}").front().flat,
          "Stylesheet collector confused nested blocks with quoted selector delimiters");
    std::string many;
    for (int i = 0; i < 4096; ++i)
        many += ".a{}";
    check(forge::inspect_ui_stylesheet(many).size() == 4096,
          "Stylesheet block cap rejected its boundary");
    rejects([&] { forge::inspect_ui_stylesheet(many + ".b{}"); });
    forge::validate_ui_text(many + ".b{}", false);
    rejects([&] { forge::insert_ui_stylesheet_rule(many, ".b"); });
}
void font_face_sources() {
    const std::string original =
        "/* header */\n@font-face { /* keep */ font-family:Old;src:'Assets/old.ttf';"
        "font-weight:700;font-style:italic;-rmlui-face-index:0;}\n.keep{font-family:Old;}";
    const auto changed =
        forge::set_ui_stylesheet_font_face(original, 0, "MenuFace", "Assets/Font's.TTC", 1);
    check(
        changed ==
            "/* header */\n@font-face { /* keep */ font-family:MenuFace;src:\"Assets/Font's.TTC\";"
            "font-weight:700;font-style:italic;-rmlui-face-index:1;}\n.keep{font-family:Old;}",
        "Font declaration edit lost comments, native overrides or unrelated selectors");
    check(forge::remove_ui_stylesheet_font_face(original, 0) ==
              "/* header */\n\n.keep{font-family:Old;}",
          "Font removal rewrote adjacent source or alias uses");
    rejects([&] { forge::remove_ui_stylesheet_font_face(original, 1); });
    const auto values = forge::read_ui_stylesheet_font_face(changed, 0);
    check(values.at("font-family") == "MenuFace" && values.at("src") == "\"Assets/Font's.TTC\"" &&
              values.at("-rmlui-face-index") == "1" &&
              forge::set_ui_stylesheet_font_face(changed, 0, "MenuFace", "Assets/Font's.TTC", 1) ==
                  changed,
          "Font declaration read/no-op changed authored literals");
    const auto added =
        forge::set_ui_stylesheet_font_face(".keep{}", {}, "MenuFace", "Assets/font.ttf", 0);
    check(added.starts_with(".keep{}\n@font-face {") &&
              forge::inspect_ui_stylesheet(added).size() == 2,
          "Font face append changed existing blocks or lost its separator");
    const std::string unchanged = "@font-face {font-family:'MenuFace';src:'Assets/font.ttf';}";
    check(forge::set_ui_stylesheet_font_face(unchanged, 0, "MenuFace", "Assets/font.ttf", 0) ==
              unchanged,
          "Unchanged font controls rewrote quoted literals or introduced a default override");
    check(forge::read_ui_stylesheet_font_face(
              forge::set_ui_stylesheet_font_face(
                  "@font-face{font-family:Menu;src:Assets/font,one.ttf;}", 0, "Menu",
                  "Assets/font,one.ttf", 0),
              0)
                  .at("src") == "\"Assets/font,one.ttf\"",
          "Unquoted source-list separators were mistaken for one unchanged font path");
    for (const auto* family : {"", "Menu Face", "9Menu", "Menu;bad", "Menu\"bad"})
        rejects([&] { forge::set_ui_stylesheet_font_face(original, 0, family, "font.ttf", 0); });
    for (const auto* path : {"../escape.ttf", "/font.ttf", "font.png"})
        rejects([&] { forge::set_ui_stylesheet_font_face(original, 0, "Menu", path, 0); });
    rejects([&] { forge::set_ui_stylesheet_font_face(original, 0, "Menu", "font.ttf", 32); });
    rejects([&] { forge::set_ui_stylesheet_font_face(original, 1, "Menu", "font.ttf", 0); });
    rejects([&] {
        forge::set_ui_stylesheet_font_face("@font-face{font-family:A;font-family:B;}", 0, "Menu",
                                           "font.ttf", 0);
    });
    rejects([&] {
        forge::set_ui_stylesheet_font_face("@font-face{.nested{}}", 0, "Menu", "font.ttf", 0);
    });
}
void font_face_style_sources() {
    const std::string source = "/* keep "
                               "*/"
                               "@font-face{font-family:Menu;src:'font.ttf';font-weight:400.0;font-"
                               "style:normal;custom:yes;} .text{font-weight:700;}";
    check(forge::set_ui_stylesheet_font_face_style(source, 0, {{"font-weight", "400"}}) == source,
          "Equal font declaration weight changed existing numeric spelling");
    const auto styled = forge::set_ui_stylesheet_font_face_style(
        source, 0, {{"font-weight", "all"}, {"font-style", "italic"}});
    check(styled == "/* keep "
                    "*/"
                    "@font-face{font-family:Menu;src:'font.ttf';font-weight:all;font-style:italic;"
                    "custom:yes;} .text{font-weight:700;}",
          "Font style edits changed unrelated aliases, declarations or rules");
    const auto values = forge::read_ui_stylesheet_font_face(styled, 0);
    check(values.at("font-weight") == "all" && values.at("font-style") == "italic",
          "Font declaration read omitted authored weight/style");
    const auto reset = forge::set_ui_stylesheet_font_face_style(
        styled, 0, {{"font-weight", {}}, {"font-style", {}}});
    check(!forge::read_ui_stylesheet_font_face(reset, 0).contains("font-weight") &&
              !forge::read_ui_stylesheet_font_face(reset, 0).contains("font-style") &&
              reset.find("custom:yes;") != std::string::npos,
          "Font default reset rewrote other declarations or stored guessed defaults");
    for (const auto* weight : {"0", "1001", "450.5", "400px", "auto", "all;src:bad.ttf"})
        rejects([&] {
            forge::set_ui_stylesheet_font_face_style(source, 0, {{"font-weight", weight}});
        });
    rejects(
        [&] { forge::set_ui_stylesheet_font_face_style(source, 0, {{"font-style", "oblique"}}); });
    rejects([&] { forge::set_ui_stylesheet_font_face_style(source, 0, {{"color", "red"}}); });
    rejects([&] { forge::set_ui_stylesheet_font_face_style(source, 1, {{"font-weight", "700"}}); });
}
void reusable_styles() {
    const std::string source =
        "<rml><body data-model='demo'><p id='a' class='old' "
        "data-class-hot='hot' style='color:red; width:2px;'>A</p><!-- gap -->"
        "<button id='b' class=\"other\" style=\"color:blue;\">B</button>"
        "<div><span>Nested</span></div></body></rml>";
    const auto nodes = forge::inspect_ui_markup(source);
    const std::array indices{find(source, "button"), find(source, "p")};
    const auto values = forge::read_ui_markup_styles(source, nodes, indices, "color");
    check(values == std::vector<std::optional<std::string>>{"blue", "red"},
          "Group property read lost input order or mixed authored values");
    const auto widths = forge::read_ui_markup_styles(source, nodes, indices, "width");
    check(widths == std::vector<std::optional<std::string>>{std::nullopt, "2px"},
          "Group property read confused missing and authored overrides");
    auto expected = source;
    expected.replace(expected.find("color:red;"), 10, "color:#abc;");
    expected.replace(expected.find("color:blue;"), 11, "color:#abc;");
    const auto styled = forge::set_ui_markup_styles(source, indices, "color", "#abc");
    check(styled == expected &&
              forge::set_ui_markup_styles(styled, indices, "color", "#abc") == styled,
          "Atomic group styles rewrote unrelated source or changed an exact no-op");
    expected = source;
    expected.erase(expected.find("color:red;"), 10);
    expected.erase(expected.find("color:blue;"), 11);
    check(forge::set_ui_markup_styles(source, indices, "color", {}) == expected &&
              forge::set_ui_markup_styles(source, indices, "absent", {}) == source,
          "Group style removal damaged other declarations or missing-property history");
    rejects([&] { forge::set_ui_markup_styles(source, indices, "font-family", "\"Lato\""); });
    check(forge::read_ui_markup_classes(source, nodes, indices) ==
              std::vector<std::optional<std::string>>{"other", "old"},
          "Group class read lost authored values");
    expected = source;
    expected.replace(expected.find("class='old'"), 11, "class=\"Base hover_2\"");
    expected.replace(expected.find("class=\"other\""), 13, "class=\"Base hover_2\"");
    check(forge::set_ui_markup_classes(source, indices, "Base hover_2") == expected &&
              expected.find("data-class-hot='hot'") != std::string::npos,
          "Group class assignment changed IDs, runtime bindings or unrelated source");
    const std::array paragraph{find(source, "p")};
    check(forge::set_ui_markup_classes(source, paragraph, "old") == source,
          "An identical logical class value rewrote original quote/attribute bytes");
    expected = source;
    expected.erase(expected.find("class='old'"), 11);
    expected.erase(expected.find("class=\"other\""), 13);
    check(forge::set_ui_markup_classes(source, indices, {}) == expected &&
              forge::set_ui_markup_classes(expected, indices, {}) == expected,
          "Group class clearing changed unrelated source or missing values");
    const std::string encoded = "<rml><body><p class='one&#32;two'>X</p></body></rml>";
    const auto encoded_nodes = forge::inspect_ui_markup(encoded);
    const std::array encoded_index{find(encoded, "p")};
    check(forge::read_ui_markup_classes(encoded, encoded_nodes, encoded_index).front() ==
                  "one two" &&
              forge::set_ui_markup_classes(encoded, encoded_index, "one two") == encoded,
          "Class read/no-op damaged an existing encoded value");
    for (const auto* value : {"", " ", "bad:name", "9first", "a\tb", "a a", "{{bound}}", "café"})
        rejects([&] { forge::set_ui_markup_classes(source, indices, value); });
    rejects([&] { forge::set_ui_markup_classes(source, indices, std::string(65, 'a')); });
    rejects([&] { forge::set_ui_markup_classes(source, indices, std::string(1025, 'a')); });
    std::string classes;
    for (int i = 0; i < 32; ++i)
        classes += "c" + std::to_string(i) + ' ';
    check(forge::set_ui_markup_classes(source, indices, classes).find("class=\"") !=
              std::string::npos,
          "Class-list limit rejected its32-token boundary");
    rejects([&] { forge::set_ui_markup_classes(source, indices, classes + "extra"); });
    for (const auto* bad : {"<rml><body><p class='{{bound}}'>X</p></body></rml>",
                            "<rml><body><p class='&#123;&#123;bound}}'>X</p></body></rml>",
                            "<rml><body><p class='a' class='b'>X</p></body></rml>",
                            "<rml><body><p class>X</p></body></rml>"}) {
        const std::string text(bad);
        const std::array selection{find(text, "p")};
        rejects([&] { forge::set_ui_markup_classes(text, selection, "new"); });
    }
    const std::vector<std::vector<std::size_t>> invalid{{},
                                                        {indices[0], indices[0]},
                                                        {indices[0], find(source, "body")},
                                                        {indices[0], find(source, "span")},
                                                        std::vector<std::size_t>(33, indices[0])};
    for (const auto& selection : invalid) {
        rejects([&] { forge::set_ui_markup_styles(source, selection, "color", "red"); });
        rejects([&] { forge::read_ui_markup_styles(source, nodes, selection, "color"); });
        rejects([&] { forge::set_ui_markup_classes(source, selection, "new"); });
        rejects([&] { forge::read_ui_markup_classes(source, nodes, selection); });
    }
    const std::string rules = "/* lead */ .a{color:red;} /* between */ .b{color:blue;} /* tail */";
    std::string thirty_two = "<rml><body>";
    for (int i = 0; i < 32; ++i)
        thirty_two += "<p>Member</p>";
    thirty_two += "</body></rml>";
    std::vector<std::size_t> selection;
    for (std::size_t i = 0; i < 32; ++i)
        selection.push_back(33 - i);
    auto all = forge::set_ui_markup_styles(thirty_two, selection, "color", "red");
    all = forge::set_ui_markup_classes(all, selection, "shared");
    const auto all_nodes = forge::inspect_ui_markup(all);
    const auto all_styles = forge::read_ui_markup_styles(all, all_nodes, selection, "color");
    const auto all_classes = forge::read_ui_markup_classes(all, all_nodes, selection);
    check(all_styles.size() == 32 && all_classes.size() == 32 &&
              std::ranges::all_of(all_styles, [](const auto& v) { return v == "red"; }) &&
              std::ranges::all_of(all_classes, [](const auto& v) { return v == "shared"; }),
          "Atomic group styling failed its32-distinct-member boundary");
    const std::string budget = "<rml><body><p data-note='" + std::string(8100, 'x') +
                               "'>A</p><button>B</button></body></rml>";
    forge::validate_ui_text(budget, true);
    const std::array budget_group{find(budget, "button"), find(budget, "p")};
    rejects([&] {
        forge::set_ui_markup_styles(budget, budget_group, "font-family",
                                    "'" + std::string(200, 'a') + "'");
    });
    rejects([&] {
        forge::set_ui_markup_classes(budget, budget_group,
                                     std::string(60, 'a') + ' ' + std::string(60, 'b') + ' ' +
                                         std::string(60, 'c'));
    });
    check(forge::duplicate_ui_stylesheet_rule(rules, 0) ==
              "/* lead */ .a{color:red;}\n.a{color:red;} /* between */ .b{color:blue;} /* tail */",
          "Rule copying changed original selectors, body or outside comments");
    const auto swapped = "/* lead */ .b{color:blue;} /* between */ .a{color:red;} /* tail */";
    check(forge::move_ui_stylesheet_rule(rules, 0, 1) == swapped &&
              forge::move_ui_stylesheet_rule(rules, 1, -1) == swapped &&
              forge::move_ui_stylesheet_rule(rules, 0, -1) == rules &&
              forge::move_ui_stylesheet_rule(rules, 1, 1) == rules,
          "Rule movement changed block/interstitial bytes or boundary no-ops");
    const std::string same = ".a{} /* between */ .a{}";
    check(forge::move_ui_stylesheet_rule(same, 0, 1) == same,
          "Identical rule movement changed source bytes");
    rejects([&] { forge::move_ui_stylesheet_rule(rules, 0, 2); });
    rejects([&] { forge::move_ui_stylesheet_rule(".a{} @media(x){.b{}}", 0, 1); });
    rejects([&] { forge::duplicate_ui_stylesheet_rule("@media(x){.b{}}", 0); });
    std::string capped;
    for (int i = 0; i < 4096; ++i)
        capped += ".a{}";
    rejects([&] { forge::duplicate_ui_stylesheet_rule(capped, 0); });
}
void common_controls() {
    using namespace forge;
    const std::string source = "<!-- keep --><rml><body><div><p>Tail</p></div>"
                               "<div id='outside'></div></body></rml>";
    const std::array<std::array<const char*, 3>, 17> cases{{
        {"container", "A & <B>", "<div>A &amp; &lt;B&gt;</div>"},
        {"inline-text", "A & <B>", "<span>A &amp; &lt;B&gt;</span>"},
        {"paragraph", "A & <B>", "<p>A &amp; &lt;B&gt;</p>"},
        {"button", "A & <B>", "<button>A &amp; &lt;B&gt;</button>"},
        {"input-button", "A & <B>", "<input type=\"button\" value=\"A &amp; &lt;B&gt;\"/>"},
        {"range", "12.5", "<input type=\"range\" value=\"12.5\"/>"},
        {"label", "A & <B>", "<label>A &amp; &lt;B&gt;</label>"},
        {"text-input", "A & <B>", "<input type=\"text\" value=\"A &amp; &lt;B&gt;\"/>"},
        {"password-input", "A & <B>", "<input type=\"password\" value=\"A &amp; &lt;B&gt;\"/>"},
        {"checkbox", "A & <B>", "<input type=\"checkbox\" value=\"A &amp; &lt;B&gt;\"/>"},
        {"radio", "A & <B>", "<input type=\"radio\" value=\"A &amp; &lt;B&gt;\"/>"},
        {"textarea", "A & <B>", "<textarea>A &amp; &lt;B&gt;</textarea>"},
        {"select", "A & <B>",
         "<select><option value=\"A &amp; &lt;B&gt;\" selected=\"selected\">A &amp; "
         "&lt;B&gt;</option></select>"},
        {"progress", "", "<progress value=\"0\"></progress>"},
        {"image", "", "<img/>"},
        {"line-break", "", "<br/>"},
        {"form", "A & <B>", "<form>A &amp; &lt;B&gt;</form>"},
    }};
    for (const auto& [kind, text, markup] : cases) {
        const auto created = insert_ui_markup_control(source, 2, kind, text, 3);
        auto expected = source;
        expected.insert(expected.find("<p>"), markup);
        const auto nodes = inspect_ui_markup(created.source);
        check(created.source == expected && created.element == 3 && nodes[3].parent == 2,
              "Common control creation lost source bytes, initial value or selected root");
    }
    check(insert_ui_markup_control(source, 2, "range")
                  .source.find("<input type=\"range\" value=\"0\"/>") != std::string::npos,
          "Slider creation lost its numeric default");
    for (const auto* value : {"nan", "inf", "1000001", "1oops", "1 2", "<input/>"})
        rejects([&] { insert_ui_markup_control(source, 2, "range", value); });
    const std::string select = "<rml><body><select></select></body></rml>";
    const auto option = insert_ui_markup_control(select, 2, "option", "First & second");
    check(option.source == "<rml><body><select><option value=\"First &amp; second\">First &amp; "
                           "second</option></select></body></rml>" &&
              option.element == 3,
          "Dropdown option creation lost escaped label/value or root index");
    const auto empty = insert_ui_markup_control(source, 2, "select");
    check(empty.source.find("<select></select>") != std::string::npos && empty.element == 4,
          "Empty dropdown unexpectedly created an option or lost append selection");
    const auto quoted = insert_ui_markup_control(source, 2, "text-input", "\"/><p>Injected");
    check(inspect_ui_markup(quoted.source).size() == inspect_ui_markup(source).size() + 1 &&
              quoted.source.find("value=\"&quot;/&gt;&lt;p&gt;Injected\"") != std::string::npos,
          "Initial control value escaped its quoted attribute");
    rejects([&] { insert_ui_markup_control(source, 2, "unknown"); });
    rejects([&] { insert_ui_markup_control(source, 99, "button"); });
    rejects([&] { insert_ui_markup_control(source, 0, "button"); });
    rejects([&] { insert_ui_markup_control(source, 2, "button", "", 4); });
    rejects([&] { insert_ui_markup_control(source, 2, "option", "Option"); });
    rejects([&] { insert_ui_markup_control(select, 2, "button", "Button"); });
    for (const auto* tag : {"textarea", "option", "progress", "input"}) {
        const std::string nested =
            "<rml><body><" + std::string(tag) +
            (std::string_view(tag) == "input" ? "/></body></rml>"
                                              : "></" + std::string(tag) + "></body></rml>");
        rejects([&] { insert_ui_markup_control(nested, 2, "button", "Button"); });
    }
    for (const auto* kind : {"image", "line-break", "progress"})
        rejects([&] { insert_ui_markup_control(source, 2, kind, "unexpected"); });
    check(insert_ui_markup_control(source, 2, "paragraph", std::string(8192, 'a')).element == 4,
          "Maximum supported initial text was rejected");
    rejects([&] { insert_ui_markup_control(source, 2, "paragraph", std::string(8193, 'a')); });
}
void configured_control_creation() {
    using namespace forge;
    const std::string source =
        "<!-- retain -->\r\n<rml><body><div id='taken'></div></body></rml>\r\n";
    const auto old = insert_ui_markup_control(source, 1, "button", "Run & test");
    check(old.source ==
              insert_ui_markup_control(source, 1, "button", "Run & test", {}, "", {}).source,
          "Empty creation options changed legacy source bytes");
    const auto named = insert_ui_markup_control(source, 1, "button", "Run & test", {}, "action");
    check(named.source.find("id=\"action\"") != named.source.npos &&
              named.source.find("position:") == named.source.npos &&
              named.source.starts_with("<!-- retain -->\r\n"),
          "Named flow control acquired implicit layout or changed unrelated source");
    const UiPixelLayout pixels{24, 36, 160, 40};
    const auto ready =
        insert_ui_markup_control(source, 1, "button", "Run & test", {}, "action", pixels);
    check(read_ui_markup_layout(ready.source, ready.element) == pixels &&
              ready.source.find("id='taken'") != ready.source.npos &&
              ready.source.find("Run &amp; test") != ready.source.npos &&
              ready.source.ends_with("</body></rml>\r\n"),
          "Pixel creation lost shared source geometry, escaping or unrelated bytes");
    rejects([&] { insert_ui_markup_control(source, 1, "button", "", {}, "taken"); });
    rejects([&] { insert_ui_markup_control(source, 1, "button", "", {}, "", pixels); });
    rejects([&] { insert_ui_markup_control(source, 1, "line-break", "", {}, "line", pixels); });
    for (const auto* id : {"bad{id}", "bad\nid"})
        rejects([&] { insert_ui_markup_control(source, 1, "button", "", {}, id); });
    rejects([&] { insert_ui_markup_control(source, 1, "button", "", {}, std::string(129, 'a')); });
    check(!insert_ui_markup_control(source, 1, "button", "", {}, std::string(128, 'a'))
               .source.empty(),
          "Maximum source ID length rejected");
    for (const auto invalid : {UiPixelLayout{0, 0, 0, 40}, UiPixelLayout{65537, 0, 160, 40},
                               UiPixelLayout{0, 0, 160, std::numeric_limits<float>::infinity()}})
        rejects([&] { insert_ui_markup_control(source, 1, "button", "", {}, "action", invalid); });
    const std::string bound = "<rml><body data-if='shown'></body></rml>";
    rejects([&] { insert_ui_markup_control(bound, 1, "button", "", {}, "action", pixels); });
    const std::string options = "<rml><body><select></select></body></rml>";
    rejects(
        [&] { insert_ui_markup_control(options, 2, "option", "Choice", {}, "choice", pixels); });
}
void typography_literals() {
    using namespace forge;
    for (const auto property : ui_common_style_properties)
        check(ui_style_literal_profile(property).has_value(),
              "Common CSS menu lacks a literal profile");
    check(!ui_style_literal_profile("custom-property"),
          "Unknown CSS property acquired a fabricated profile");
    const auto weight = *ui_style_literal_profile("font-weight");
    check(weight.integer && weight.minimum == 1 && weight.maximum == 1000 &&
              weight.units.size() == 1 && weight.units.front() == UiStyleUnit::Number &&
              weight.keywords.size() == 2 && weight.keywords[1] == "bold" &&
              format_ui_style_literal_number("font-weight", weight.initial) == "400",
          "Font-weight literal profile differs from its bounded source controls");
    for (const auto* text : {"0", "1001", "700.25", "700px", "700%", "bold"})
        check(!read_ui_style_literal_number("font-weight", text),
              "Unsupported weight became a typed number");
    for (const auto text : {"1", "400", "700", "1000"})
        check(read_ui_style_literal_number("font-weight", text).has_value(),
              "Whole-number weight rejected");
    rejects([&] { format_ui_style_literal_number("font-weight", {700.25, UiStyleUnit::Number}); });
    rejects([&] { format_ui_style_literal_number("font-weight", {700, UiStyleUnit::Pixels}); });
    check(read_ui_style_literal_number("letter-spacing", "-1.25dp").has_value() &&
              !read_ui_style_literal_number("letter-spacing", "10%") &&
              !read_ui_style_literal_number("letter-spacing", "1em") &&
              read_ui_style_literal_number("line-height", "120%").has_value() &&
              !read_ui_style_literal_number("line-height", "-1px"),
          "Spacing controls guessed unsupported units or changed the bounded profile");
    for (const auto name :
         {"text-align", "font-style", "text-decoration", "text-transform", "white-space"}) {
        const auto profile = *ui_style_literal_profile(name);
        check(!profile.keywords.empty() && profile.units.empty(),
              "Typography keyword control is missing");
        check(!read_ui_style_literal_number(name, "1"),
              "Keyword property advertised a numeric helper");
    }
    for (const auto property : {"opacity", "font-size", "width", "height", "padding", "margin",
                                "left", "top", "right", "bottom"}) {
        const auto profile = *ui_style_literal_profile(property);
        check(profile.units.size() == 4 && profile.minimum == -65536 && profile.maximum == 65536 &&
                  read_ui_style_literal_number(property, "-12.5dp") ==
                      read_ui_style_number("-12.5dp"),
              "Existing numeric literal controls changed their supported envelope");
    }
    const std::string original = "<!-- keep --><rml><body><p id='text' "
                                 "style='font-weight:400;color:#ffffff'>Caption</p></body></rml>";
    const auto changed = set_ui_markup_style(original, 2, "font-weight", "bold");
    check(changed.find("font-weight:bold;color:#ffffff") != changed.npos &&
              changed.find("id='text'") != changed.npos &&
              changed.ends_with(">Caption</p></body></rml>"),
          "Typography preset source write replaced unrelated source");
}
void responsive_layout_literals() {
    using namespace forge;
    check(ui_common_style_properties.size() == 53,
          "Responsive layout controls changed the shared menu unexpectedly");
    for (const auto* name : {"display", "flex-direction", "flex-wrap", "justify-content",
                             "align-content", "align-items", "align-self"}) {
        const auto profile = ui_style_literal_profile(name);
        check(profile && !profile->keywords.empty() && profile->units.empty(),
              "Native layout keyword presets missing");
    }
    const auto self = *ui_style_literal_profile("align-self");
    const auto items = *ui_style_literal_profile("align-items");
    check(self.keywords.front() == "auto" && items.keywords.front() == "flex-start" &&
              ui_style_literal_profile("justify-content")->keywords.back() == "space-evenly" &&
              ui_style_literal_profile("align-content")->keywords.back() == "stretch",
          "Alignment helpers mixed native property keyword domains");
    for (const auto* name : {"flex-grow", "flex-shrink"}) {
        check(read_ui_style_literal_number(name, "1.5").has_value() &&
                  !read_ui_style_literal_number(name, "-1") &&
                  !read_ui_style_literal_number(name, "1px") &&
                  !read_ui_style_literal_number(name, "1%") &&
                  !read_ui_style_literal_number(name, "NaN"),
              "Flex factors guessed units or admitted unsupported negative values");
        rejects([&] { format_ui_style_literal_number(name, {1, UiStyleUnit::Pixels}); });
    }
    for (const auto* name : {"flex-basis", "row-gap", "column-gap", "min-width", "max-width",
                             "min-height", "max-height"}) {
        check(read_ui_style_literal_number(name, "0px").has_value() &&
                  read_ui_style_literal_number(name, "12.5dp").has_value() &&
                  read_ui_style_literal_number(name, "50%").has_value() &&
                  !read_ui_style_literal_number(name, "12") &&
                  !read_ui_style_literal_number(name, "-1px") &&
                  !read_ui_style_literal_number(name, "65537px"),
              "Responsive dimensions lost their bounded length/percentage profile");
        rejects([&] { format_ui_style_literal_number(name, {-1, UiStyleUnit::Pixels}); });
    }
    check(ui_style_literal_profile("flex-basis")->keywords.front() == "auto" &&
              ui_style_literal_profile("max-width")->keywords.front() == "none" &&
              ui_style_literal_profile("min-width")->keywords.empty(),
          "Native dimension reset presets were fabricated");
    // Raw source stays broader than the optional controls, including compound shorthands.
    const std::string source = "<rml><body><div style='color:red'></div></body></rml>";
    const auto custom = set_ui_markup_style(source, 2, "flex", "1 0 auto");
    check(custom.find("flex:1 0 auto") != custom.npos && custom.find("color:red") != custom.npos,
          "Literal profiles restricted the existing raw source owner");
}
void box_style_literals() {
    using namespace forge;
    check(ui_style_literal_profile("position")->keywords.size() == 4 &&
              ui_style_literal_profile("box-sizing")->keywords.front() == "content-box" &&
              ui_style_literal_profile("overflow-x")->keywords.back() == "scroll" &&
              ui_style_literal_profile("overflow-y")->keywords.front() == "visible" &&
              ui_style_literal_profile("border-color")->color,
          "Box presets and color controls differ from supported native literal domains");
    for (const auto* name : {"padding-top", "padding-right", "padding-bottom", "padding-left"})
        check(read_ui_style_literal_number(name, "5%").has_value() &&
                  read_ui_style_literal_number(name, "1dp").has_value() &&
                  !read_ui_style_literal_number(name, "-1px") &&
                  !read_ui_style_literal_number(name, "1"),
              "Padding helper admitted negative or unitless source numbers");
    for (const auto* name : {"margin-top", "margin-right", "margin-bottom", "margin-left"})
        check(read_ui_style_literal_number(name, "-5%").has_value() &&
                  ui_style_literal_profile(name)->keywords.front() == "auto",
              "Margin helper lost native negative values or auto preset");
    for (const auto* name : {"border-width", "border-radius"}) {
        check(read_ui_style_literal_number(name, "4dp").has_value() &&
                  !read_ui_style_literal_number(name, "4%") &&
                  !read_ui_style_literal_number(name, "-1px") &&
                  !read_ui_style_literal_number(name, "4"),
              "Border helper invented percentage/unitless or negative support");
        rejects([&] { format_ui_style_literal_number(name, {4, UiStyleUnit::Percent}); });
    }
    const std::string source = "<rml><body><div style='padding:1px 2px'></div></body></rml>";
    const auto text = set_ui_markup_style(source, 2, "border-width", "1px 2px 3px 4px");
    check(text.find("padding:1px 2px") != text.npos &&
              text.find("border-width:1px 2px 3px 4px") != text.npos,
          "Simple border profile removed raw compound source values");
}
void style_value_literals() {
    using namespace forge;
    const std::array cases{
        std::pair{"0", UiStyleNumber{0, UiStyleUnit::Number}},
        std::pair{" -12.5px\t", UiStyleNumber{-12.5, UiStyleUnit::Pixels}},
        std::pair{"24dp", UiStyleNumber{24, UiStyleUnit::DensityPixels}},
        std::pair{"33.125%", UiStyleNumber{33.125, UiStyleUnit::Percent}},
        std::pair{"65536px", UiStyleNumber{65536, UiStyleUnit::Pixels}},
        std::pair{"-65536", UiStyleNumber{-65536, UiStyleUnit::Number}},
        std::pair{"0.10000000000000002", UiStyleNumber{0.10000000000000002, UiStyleUnit::Number}}};
    for (const auto& [text, expected] : cases) {
        const auto parsed = read_ui_style_number(text);
        check(parsed == expected && read_ui_style_number(format_ui_style_number(*parsed)) == parsed,
              "Literal number/unit adapter lost a value or round-trip precision");
    }
    for (const auto* invalid :
         {"", "px", "12 px", "12PX", "12px trailing", "auto", "1em", "calc(2px + 3px)", "{{value}}",
          "NaN", "inf", "1e9999", "65537px", "-65536.1%", "1; color:red", "1 2"})
        check(!read_ui_style_number(invalid), "Unsupported style number acquired a guessed value");
    for (const auto value : {std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN(), 65537., -65537.})
        rejects([&] { format_ui_style_number({value, UiStyleUnit::Pixels}); });
    rejects([&] { format_ui_style_number({1, static_cast<UiStyleUnit>(99)}); });
    check(format_ui_style_number({24, UiStyleUnit::Pixels}) == "24px" &&
              format_ui_style_number({.5, UiStyleUnit::Number}) == "0.5",
          "Literal number formatter changed suffix or decimal spelling");
    check(read_ui_style_color("#000000") == UiStyleColor{0, 0, 0, 1} &&
              read_ui_style_color("#ff000000") == UiStyleColor{1, 0, 0, 0} &&
              read_ui_style_color("#00ff00ff") == UiStyleColor{0, 1, 0, 1} &&
              read_ui_style_color(" #0000ff80\n") == UiStyleColor{0, 0, 1, 128.f / 255},
          "Literal color adapter confused RGB channel order or final alpha");
    for (const auto* text : {"#00000000", "#01020304", "#abcdef", "#ABCDEF80", "#ffffffff"}) {
        const auto parsed = read_ui_style_color(text);
        check(parsed && read_ui_style_color(format_ui_style_color(*parsed)) == parsed,
              "Literal color failed byte-exact normalized channel round trip");
    }
    check(format_ui_style_color({0, 0, 0, 0}) == "#00000000" &&
              format_ui_style_color({1, 1, 1, 1}) == "#ffffff" &&
              format_ui_style_color({0, 0, 1, .5}) == "#0000ff80",
          "Color formatter lost leading zeros, opaque spelling or alpha rounding");
    for (const auto* invalid :
         {"", "#fff", "#abcd", "#gg0000", "#1234567", "#123456789", "#12345g", "#123456;", "red",
          "transparent", "rgb(1,2,3)", "{{color}}", "0xffffff", "#-12345"})
        check(!read_ui_style_color(invalid), "Unsupported color acquired guessed channels");
    for (const auto value : {std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN(), -0.01f, 1.01f})
        for (std::size_t channel = 0; channel < 4; ++channel) {
            UiStyleColor color{0, 0, 0, 1};
            color[channel] = value;
            rejects([&] { format_ui_style_color(color); });
        }
}
} // namespace
void control_properties() {
    using namespace forge;
    using Value = UiControlPropertyValue;
    const auto read = [](std::string_view source) {
        const auto nodes = inspect_ui_markup(source);
        return read_ui_markup_control_properties(source, nodes, 2);
    };
    const auto property = [](const UiMarkupControlProperties& state, std::string_view name) {
        for (const auto& item : state.properties)
            if (item.name == name)
                return item;
        throw std::runtime_error("Missing source control property");
    };
    const std::string initial =
        "<!-- keep --><rml><body><input id='stable' type='range' value='1e1'/>"
        "<p id='outside'>Keep &amp; retain</p></body></rml>";
    const auto before = read(initial);
    check(before.kind == "range" && before.label == "Slider" && before.properties.size() == 8 &&
              property(before, "value").value == Value(10.0) &&
              property(before, "value").authored && property(before, "max").value == Value(100.0) &&
              !property(before, "max").authored &&
              property(before, "orientation").value == Value(std::string("horizontal")) &&
              std::holds_alternative<std::monostate>(property(before, "tab-index").value),
          "Source property defaults/metadata or authored facts differ");
    check(set_ui_markup_control_properties(initial, 2, {{"value", 10.0}}) == initial,
          "Equal native-float property normalized source spelling or quotes");
    auto changed = set_ui_markup_control_properties(initial, 2,
                                                    {{"min", -2.0},
                                                     {"max", 8.0},
                                                     {"step", 2.0},
                                                     {"disabled", true},
                                                     {"orientation", std::string("vertical")},
                                                     {"tab-index", std::string("none")}});
    auto state = read(changed);
    check(property(state, "value").value == Value(10.0) &&
              changed.find("value='1e1'") != changed.npos && changed.starts_with("<!-- keep -->") &&
              changed.find("id='stable'") != changed.npos &&
              changed.find("<p id='outside'>Keep &amp; retain</p>") != changed.npos &&
              property(state, "disabled").value == Value(true) &&
              property(state, "tab-index").value == Value(std::string("none")),
          "Composed control properties changed unrelated source or clamped authored values");
    changed = set_ui_markup_control_properties(
        changed, 2, {{"disabled", false}, {"min", Value{}}, {"tab-index", Value{}}});
    state = read(changed);
    check(!property(state, "disabled").authored && !property(state, "min").authored &&
              !property(state, "tab-index").authored && property(state, "min").value == Value(0.0),
          "Clearing source properties materialized defaults or retained flags");
    const std::string flags =
        "<rml><body><input type='checkbox' checked='false' disabled/></body></rml>";
    check(property(read(flags), "checked").value == Value(true) &&
              property(read(flags), "disabled").value == Value(true) &&
              set_ui_markup_control_properties(flags, 2, {{"checked", true}, {"disabled", true}}) ==
                  flags,
          "Native source boolean presence was replaced by string truthiness");
    check(
        !property(read(set_ui_markup_control_properties(flags, 2, {{"checked", false}})), "checked")
             .authored,
        "False source flag wrote a present false attribute");
    const std::string caption =
        "<rml><body><input type='button' value='A &amp; &lt;B&gt;'/></body></rml>";
    check(property(read(caption), "value").value == Value(std::string("A & <B>")) &&
              set_ui_markup_control_properties(caption, 2, {{"value", std::string("A & <B>")}}) ==
                  caption,
          "Control caption decoded twice or normalized equal source");
    const auto escaped =
        set_ui_markup_control_properties(caption, 2, {{"value", std::string("C & <D>")}});
    check(escaped.find("C &amp; &lt;D&gt;") != escaped.npos &&
              inspect_ui_markup(escaped).size() == 3,
          "Control caption escaped its attribute");
    const std::string progress = "<rml><body><progress value='20' max='0'/></body></rml>";
    check(property(read(progress), "value").value == Value(20.0) &&
              property(read(progress), "max").value == Value(0.0) &&
              read(progress).properties.size() == 2,
          "Source progress adapter copied native runtime clamping/fallback into authored state");
    const std::string radio =
        "<rml><body><input type='radio' name='a&amp;b' value='one'/></body></rml>";
    check(property(read(radio), "name").value == Value(std::string("a&b")),
          "Radio group source did not decode");
    const std::string bound =
        "<rml><body><input type='range' value='3' data-value='amount'/></body></rml>";
    const auto bound_value = property(read(bound), "value");
    check(bound_value.authored && !bound_value.available && !bound_value.editable &&
              !bound_value.reason.empty(),
          "Bound value advertised an editable literal or lost authored presence");
    rejects([&] { set_ui_markup_control_properties(bound, 2, {{"value", 4.0}}); });
    check(
        property(read(set_ui_markup_control_properties(bound, 2, {{"disabled", true}})), "disabled")
                .value == Value(true),
        "Unrelated source binding blocked a literal disabled property");
    const std::string duplicate = "<rml><body><input type='range' min='0' min='2'/></body></rml>";
    check(!property(read(duplicate), "min").editable,
          "Duplicate control attribute advertised editing");
    rejects([&] { set_ui_markup_control_properties(duplicate, 2, {{"min", 2.0}}); });
    const std::string custom =
        "<rml><body><input type='range' step='0' orientation='diagonal'/></body></rml>";
    check(!property(read(custom), "step").available && property(read(custom), "step").editable &&
              !property(read(custom), "orientation").available,
          "Unsupported source literals were represented as native defaults");
    check(property(read(set_ui_markup_control_properties(
                       custom, 2, {{"step", 1.0}, {"orientation", std::string("horizontal")}})),
                   "step")
              .available,
          "Explicit valid source property replacement could not repair a literal");
    for (const auto* type : {"RANGE", "submit"}) {
        const std::string unsupported = "<rml><body><input type='" + std::string(type) +
                                        "' value='private-source'/></body></rml>";
        check(read(unsupported).properties.empty() && !read(unsupported).reason.empty(),
              "Non-profile source input was advertised as a control property panel");
        rejects([&] {
            set_ui_markup_control_properties(unsupported, 2, {{"value", std::string("other")}});
        });
    }
    const std::string options =
        "<rml><body><select><option selected value='x'>Choice</option></select></body></rml>";
    const auto option_nodes = inspect_ui_markup(options);
    const auto option_properties = read_ui_markup_control_properties(options, option_nodes, 3);
    check(option_properties.properties.size() == 3 &&
              property(option_properties, "selected").value == Value(true),
          "Dropdown option selected presence or source profile differs");
    const auto option_changed = set_ui_markup_control_properties(options, 3, {{"selected", false}});
    auto option_expected = options;
    option_expected.erase(option_expected.find("selected"), std::string_view("selected").size());
    check(option_changed == option_expected,
          "Option flag removal changed interstitial source, caption or value bytes");
    check(read("<rml><body><option>Orphan</option></body></rml>").properties.empty(),
          "Orphan option advertised native dropdown properties");
    const std::string plain = "<rml><body><button>Caption</button></body></rml>";
    check(read(plain).properties.size() == 2,
          "Generic button acquired native disabled/value properties");
    rejects([&] { set_ui_markup_control_properties(plain, 2, {{"disabled", true}}); });
    for (double value : {std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN(), 1000001.})
        rejects([&] { set_ui_markup_control_properties(initial, 2, {{"value", value}}); });
    for (double value : {0., -1., 0.0000001})
        rejects([&] { set_ui_markup_control_properties(initial, 2, {{"step", value}}); });
    rejects([&] {
        set_ui_markup_control_properties(initial, 2,
                                         {{"min", 1.0}, {"orientation", std::string("diagonal")}});
    });
    rejects([&] { set_ui_markup_control_properties(initial, 2, {{"value", std::string("2")}}); });
    rejects([&] { set_ui_markup_control_properties(initial, 2, {{"checked", true}}); });
    rejects([&] {
        set_ui_markup_control_properties(caption, 2, {{"value", std::string("{{dynamic}}")}});
    });
    rejects(
        [&] { set_ui_markup_control_properties(caption, 2, {{"value", std::string(8193, 'a')}}); });
    rejects(
        [&] { set_ui_markup_control_properties(caption, 2, {{"value", std::string("a\0b", 3)}}); });
}

void image_control_properties() {
    using namespace forge;
    using Value = UiControlPropertyValue;
    const auto read = [](std::string_view source) {
        return read_ui_markup_control_properties(source, inspect_ui_markup(source), 2);
    };
    const auto row = [](const UiMarkupControlProperties& state, std::string_view name) {
        for (const auto& value : state.properties)
            if (value.name == name)
                return value;
        throw std::runtime_error("Missing image source property");
    };
    const std::string source =
        "<!-- keep --><rml><body><img id='photo' src='old.tga'/><p>Keep</p></body></rml>";
    const auto before = read(source);
    check(before.kind == "image" && before.properties.size() == 5 &&
              row(before, "src").value == Value(std::string("old.tga")) &&
              !row(before, "width").available && !row(before, "width").authored &&
              row(before, "width").editable &&
              std::holds_alternative<std::monostate>(row(before, "width").value),
          "Image properties guessed unloaded intrinsic dimensions or acquired native form flags");
    const auto patched = set_ui_markup_control_properties(source, 2,
                                                          {{"src", std::string("new & safe.tga")},
                                                           {"sprite", std::string("badge")},
                                                           {"rect", std::string("0 0 2 1")},
                                                           {"width", 32.5}});
    const auto after = read(patched);
    check(
        row(after, "src").value == Value(std::string("new & safe.tga")) &&
            row(after, "sprite").value == Value(std::string("badge")) &&
            row(after, "rect").value == Value(std::string("0 0 2 1")) &&
            row(after, "width").value == Value(32.5) && !row(after, "height").available &&
            patched.starts_with("<!-- keep -->") && patched.find("id='photo'") != patched.npos &&
            patched.ends_with("<p>Keep</p></body></rml>"),
        "Image source patch changed unrelated bytes, dropped fallback fields or normalized layout");
    const auto reset = read(set_ui_markup_control_properties(
        patched, 2, {{"width", Value{}}, {"rect", Value{}}, {"sprite", Value{}}}));
    check(!row(reset, "width").authored && !row(reset, "width").available &&
              !row(reset, "rect").authored && !row(reset, "sprite").authored,
          "Reset image dimensions materialized an invented default");
    rejects([&] { set_ui_markup_control_properties(source, 2, {{"width", 1e-45}}); });
    rejects([&] { set_ui_markup_control_properties(source, 2, {{"width", 1e-300}}); });
    rejects([&] {
        set_ui_markup_control_properties(source, 2, {{"rect", std::string("0 0 1e6 1e-45")}});
    });
    rejects([&] {
        set_ui_markup_control_properties(source, 2,
                                         {{"rect", std::string("1000000 0 0.000001 1")}});
    });
    const auto zero = read(set_ui_markup_control_properties(source, 2, {{"width", 0.0}}));
    check(row(zero, "width").authored && row(zero, "width").value == Value(0.0),
          "Explicit zero image attribute was confused with an absent source value");
    for (const auto* invalid : {"0 0 2", "0 0 2 1 5", "0,0,2,1", "0 0 0 1", "0 0 2 -1", "0 0 NaN 1",
                                "0 0 1000001 1", "0\t0 2 1"})
        rejects(
            [&] { set_ui_markup_control_properties(source, 2, {{"rect", std::string(invalid)}}); });
    for (const auto* valid : {"", "0  0 2 1", "-1 0 2.5 1"})
        check(row(read(set_ui_markup_control_properties(source, 2, {{"rect", std::string(valid)}})),
                  "rect")
                  .available,
              "Valid bounded crop source literal rejected");
    rejects([&] {
        set_ui_markup_control_properties(source, 2,
                                         {{"src", std::string("other")}, {"width", -1.0}});
    });
    rejects([&] { set_ui_markup_control_properties(source, 2, {{"disabled", true}}); });
    const std::string bound = "<rml><body><img src='old.tga' data-attr-src='bound'/></body></rml>";
    check(!row(read(bound), "src").editable, "Bound image source advertised literal editing");
    rejects([&] { set_ui_markup_control_properties(bound, 2, {{"src", std::string("other")}}); });
    check(ui_style_literal_profile("image-color")->color &&
              ui_style_literal_profile("image-color")->units.empty(),
          "Image tint did not reuse the native color literal profile");
}

void text_control_properties() {
    using namespace forge;
    using Value = UiControlPropertyValue;
    const auto read = [](std::string_view source) {
        return read_ui_markup_control_properties(source, inspect_ui_markup(source), 2);
    };
    const auto row = [](const UiMarkupControlProperties& state, std::string_view name) {
        for (const auto& value : state.properties)
            if (value.name == name)
                return value;
        throw std::runtime_error("Missing text source property");
    };
    for (const auto* type : {"text", "password"}) {
        const std::string source = "<!-- keep --><rml><body><input type='" + std::string(type) +
                                   "' id='stable' value='A &amp; B' size='0020'/>"
                                   "<p>Keep this</p></body></rml>";
        const auto before = read(source);
        check(before.properties.size() == 7 &&
                  row(before, "value").value == Value(std::string("A & B")) &&
                  row(before, "maxlength").value == Value(-1.0) && row(before, "size").integer &&
                  row(before, "size").minimum == 1 && row(before, "size").maximum == 4096,
              "Text/password source metadata or native defaults differ");
        check(set_ui_markup_control_properties(source, 2, {{"size", 20.0}}) == source,
              "Equal source integer normalized its spelling");
        const auto changed =
            set_ui_markup_control_properties(source, 2,
                                             {{"placeholder", std::string("Name & <hint>")},
                                              {"maxlength", 0.0},
                                              {"size", 12.0},
                                              {"disabled", true}});
        const auto after = read(changed);
        check(row(after, "placeholder").value == Value(std::string("Name & <hint>")) &&
                  row(after, "maxlength").value == Value(0.0) &&
                  row(after, "size").value == Value(12.0) && changed.starts_with("<!-- keep -->") &&
                  changed.find("id='stable'") != changed.npos &&
                  changed.find("value='A &amp; B'") != changed.npos &&
                  changed.ends_with("<p>Keep this</p></body></rml>"),
              "Text source property batch normalized unrelated content or native values");
        const auto cleared = read(set_ui_markup_control_properties(
            changed, 2, {{"maxlength", Value{}}, {"placeholder", Value{}}, {"disabled", false}}));
        check(!row(cleared, "maxlength").authored &&
                  row(cleared, "maxlength").value == Value(-1.0) &&
                  !row(cleared, "placeholder").authored && !row(cleared, "disabled").authored,
              "Text property reset wrote defaults instead of removing attributes");
        for (const double number : {-2., 0.5, 1.000000001, 1000001.})
            rejects([&] { set_ui_markup_control_properties(source, 2, {{"maxlength", number}}); });
        for (const double number : {0., 4097., 1.5})
            rejects([&] { set_ui_markup_control_properties(source, 2, {{"size", number}}); });
    }
    const std::string area =
        "<rml><body><textarea id='notes'>Line &amp; one\nLine two</textarea></body></rml>";
    const auto before = read(area);
    check(before.properties.size() == 8 && row(before, "rows").value == Value(2.0) &&
              row(before, "cols").value == Value(20.0) &&
              row(before, "wrap").value == Value(std::string("wrap")),
          "Textarea native source defaults differ");
    const auto changed = set_ui_markup_control_properties(area, 2,
                                                          {{"rows", 6.0},
                                                           {"cols", 32.0},
                                                           {"wrap", std::string("nowrap")},
                                                           {"maxlength", 80.0},
                                                           {"placeholder", std::string("Notes")}});
    check(changed.find(">Line &amp; one\nLine two</textarea>") != changed.npos &&
              row(read(changed), "wrap").value == Value(std::string("nowrap")),
          "Textarea settings replaced its existing body or native wrap policy");
    rejects([&] {
        set_ui_markup_control_properties(area, 2, {{"value", std::string("replacement")}});
    });
    const std::string label =
        "<rml><body><label for='name' id='caption'><span>Rich caption</span></label></body></rml>";
    check(read(label).properties.size() == 1 &&
              row(read(label), "for").value == Value(std::string("name")),
          "Label target profile is missing");
    const auto target =
        set_ui_markup_control_properties(label, 2, {{"for", std::string("other&field")}});
    check(target.find("for=\"other&amp;field\"") != target.npos &&
              target.find("id='caption'><span>Rich caption</span>") != target.npos,
          "Label target change overwrote its ID or nested caption");
    for (const auto* attr :
         {"maxlength='1.5'", "maxlength='1.000000001'", "maxlength='2e1'", "size='0'"}) {
        const std::string source =
            "<rml><body><input type='text' " + std::string(attr) + "/></body></rml>";
        const char* field = std::string_view(attr).starts_with("size") ? "size" : "maxlength";
        check(!row(read(source), field).available && row(read(source), field).editable,
              "Unsupported native integer spelling silently became a rounded integer");
        check(row(read(set_ui_markup_control_properties(source, 2, {{field, 20.0}})), field)
                  .available,
              "Explicit whole-number replacement could not repair an unsupported integer");
    }
    for (const auto* source :
         {"<rml><body><input type='text' value='x' data-value='bound'/></body></rml>",
          "<rml><body><input type='password' value='x' value='y'/></body></rml>"}) {
        check(!row(read(source), "value").editable,
              "Text source binding/duplicate admitted typed editing");
        rejects([&] {
            set_ui_markup_control_properties(source, 2, {{"value", std::string("other")}});
        });
    }
}

int main() {
    try {
        ranges();
        invalid();
        edits();
        layout_edits();
        layout_group_edits();
        layout_group_geometry();
        layout_transform_geometry();
        group_removals();
        subtree_duplication();
        hierarchy_moves();
        inline_properties();
        stylesheet_rules();
        font_face_sources();
        font_face_style_sources();
        reusable_styles();
        style_value_literals();
        typography_literals();
        responsive_layout_literals();
        box_style_literals();
        common_controls();
        configured_control_creation();
        control_properties();
        text_control_properties();
        image_control_properties();
        std::cout << "Source-preserving outline/attributes and shared validator errors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
