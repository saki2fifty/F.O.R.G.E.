#include "asset_bytes.hpp"
#include "bounded_json.hpp"
#include "ui_text_boundaries.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utf8proc.h>

namespace {
using namespace forge;
void check(bool value, const std::string& why) {
    if (!value)
        throw std::runtime_error(why);
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception&) {
        rejected = true;
    }
    check(rejected, "Invalid UI text boundary input was admitted");
}
std::string utf8(std::u8string_view text) {
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
void policy() {
    check(ui_grapheme_boundaries("") == std::vector<int>{0}, "Empty input lost its boundary");
    check(ui_grapheme_boundaries(utf8(u8"a\u0301b")) == std::vector<int>({0, 2, 3}),
          "Combining input split its first grapheme");
    check(ui_grapheme_boundaries(utf8(u8"\U0001f1fa\U0001f1f8x")) == std::vector<int>({0, 2, 3}),
          "Regional indicators split the flag");
    check(ui_grapheme_boundaries(utf8(u8"\u1100\u1161\u11a8x")) == std::vector<int>({0, 3, 4}),
          "Hangul scalar input split its syllable");
    check(ui_grapheme_boundaries("\r\nx") == std::vector<int>({0, 2, 3}), "CRLF split its cluster");
    const auto emoji = utf8(u8"\U0001f469\u200d\U0001f4bbx");
    const auto range = [&](UiTextSelection selection, UiTextKey key, UiTextSelection expected) {
        check(ui_text_key_selection(emoji, selection, key) == expected,
              "Native key selection would split a grapheme");
    };
    range({0, 0}, UiTextKey::Delete, {0, 3});
    range({3, 3}, UiTextKey::Backspace, {0, 3});
    range({3, 3}, UiTextKey::Left, {0, 3});
    range({0, 0}, UiTextKey::Right, {0, 3});
    range({1, 1}, UiTextKey::Delete, {0, 3});
    range({1, 1}, UiTextKey::Backspace, {0, 3});
    range({1, 1}, UiTextKey::Left, {0, 1});
    range({1, 1}, UiTextKey::Right, {1, 3});
    range({1, 2}, UiTextKey::Delete, {0, 3});
    const auto shifted = [&](UiTextSelection before, UiTextSelection after, UiTextKey key,
                             UiTextOrientedSelection expected) {
        check(ui_text_shift_selection(emoji, before, after, key) == expected,
              "Native Shift movement lost complete-character boundaries or direction");
    };
    shifted({0, 0}, {0, 1}, UiTextKey::Right, {0, 3});
    shifted({3, 3}, {2, 3}, UiTextKey::Left, {3, 0});
    shifted({0, 3}, {0, 2}, UiTextKey::Left, {0, 0});
    shifted({0, 3}, {1, 3}, UiTextKey::Right, {3, 3});
    shifted({1, 1}, {1, 2}, UiTextKey::Right, {0, 3});
    shifted({1, 1}, {0, 1}, UiTextKey::Left, {3, 0});
    for (auto key : {UiTextKey::Left, UiTextKey::Right, UiTextKey::Backspace, UiTextKey::Delete}) {
        check(!ui_text_shift_selection(emoji, {0, 3}, {0, 3}, key),
              "Unchanged/canceled native selection was corrected");
        check(!ui_text_shift_selection(emoji, {0, 0}, {0, 3}, key),
              "Ambiguous native movement was assigned an invented anchor");
    }
    rejects([&] { ui_text_shift_selection(emoji, {-1, 0}, {0, 0}, UiTextKey::Right); });
    rejects([&] { ui_text_shift_selection(emoji, {0, 0}, {2, 1}, UiTextKey::Right); });
    rejects([&] { ui_text_shift_selection(emoji, {0, 0}, {0, 5}, UiTextKey::Right); });
    for (auto key : {UiTextKey::Left, UiTextKey::Right, UiTextKey::Backspace, UiTextKey::Delete}) {
        check(!ui_text_key_selection(emoji, {0, 3}, key),
              "Whole selection was needlessly replaced");
        check(!ui_text_key_selection("", {0, 0}, key), "Empty native key selected text");
    }
    rejects([&] { ui_text_key_selection(emoji, {-1, 0}, UiTextKey::Delete); });
    rejects([&] { ui_text_key_selection(emoji, {2, 1}, UiTextKey::Delete); });
    rejects([&] { ui_text_key_selection(emoji, {0, 5}, UiTextKey::Delete); });
    for (const auto* invalid : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80", "\xe2\x82"})
        rejects([&] { ui_grapheme_boundaries(invalid); });
    rejects([&] { ui_grapheme_boundaries(std::string(ui_edit_text_bytes + 1, 'a')); });
    rejects([&] { ui_grapheme_boundaries(std::string(ui_edit_text_characters + 1, 'a')); });
    std::string maximum;
    for (int i = 0; i < ui_edit_text_characters; ++i)
        maximum += utf8(u8"\U0001f600");
    check(maximum.size() == ui_edit_text_bytes &&
              ui_grapheme_boundaries(maximum).back() == ui_edit_text_characters,
          "The admitted byte/scalar limit was rejected");
}
unsigned conformance(const char* path) {
    check(std::string(utf8proc_version()) == "2.12.0" &&
              std::string(utf8proc_unicode_version()) == "18.0.0",
          "Unicode data/provider version differs from the selected fixture");
    const auto directory = std::filesystem::path(path).parent_path();
    const auto provenance = asset_detail::parse_bounded_json(
        asset_detail::read_bytes(directory / "provenance.json", 16384), 16384, 256, 8);
    for (const auto* name : {"GraphemeBreakTest.txt", "LICENSE.txt"})
        check(asset_detail::content_digest(
                  asset_detail::read_bytes(directory / name, 2 * 1024 * 1024)) ==
                  provenance.at("files").at(name).at("sha256").get<std::string>(),
              "Official Unicode fixture or notice bytes changed");
    std::ifstream input(path);
    check(bool(input), "Missing official grapheme test fixture");
    std::string line;
    unsigned line_number = 0, cases = 0;
    while (std::getline(input, line)) {
        ++line_number;
        check(line.size() <= 16384 && line_number <= 16384, "Invalid fixture extent");
        line.erase(line.find('#') == line.npos ? line.size() : line.find('#'));
        std::istringstream stream(line);
        std::string token, text;
        std::vector<int> expected;
        int scalars = 0;
        bool marker = true;
        while (stream >> token) {
            if (marker) {
                check(token == "\xc3\xb7" || token == "\xc3\x97", "Invalid fixture marker");
                if (token == "\xc3\xb7")
                    expected.push_back(scalars);
            } else {
                unsigned point = 0;
                const auto parsed =
                    std::from_chars(token.data(), token.data() + token.size(), point, 16);
                check(parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() &&
                          point <= 0x10ffff,
                      "Invalid fixture codepoint");
                utf8proc_uint8_t bytes[4]{};
                const auto count = utf8proc_encode_char(utf8proc_int32_t(point), bytes);
                check(count > 0, "Official scalar could not be encoded");
                text.append(reinterpret_cast<const char*>(bytes), std::size_t(count));
                ++scalars;
            }
            marker = !marker;
        }
        if (!scalars)
            continue;
        check(!marker && expected.front() == 0 && expected.back() == scalars,
              "Official case lacks start/end markers");
        check(ui_grapheme_boundaries(text) == expected,
              "UI UTF-8 boundary mismatch on official line " + std::to_string(line_number));
        ++cases;
    }
    check(input.eof() && cases == 853, "Official fixture is incomplete or changed");
    return cases;
}
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 2, "Need official Unicode 18 grapheme data");
        policy();
        const auto cases = conformance(argv[1]);
        std::cout << cases
                  << " official UTF-8 boundary cases and native key selection/bounds passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
