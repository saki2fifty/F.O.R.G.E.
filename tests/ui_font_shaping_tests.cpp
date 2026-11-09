#include "ui_font_shaping.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <hb-ot.h>
#include <hb.h>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void check(bool value, const char* why) {
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
    check(rejected, "Invalid shaping input was accepted");
}
std::string utf8(std::u8string_view text) {
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
struct Font {
    std::string bytes;
    std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)> blob{nullptr, hb_blob_destroy};
    std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)> face{nullptr, hb_face_destroy};
    std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)> font{nullptr, hb_font_destroy};
    explicit Font(const char* path) {
        std::ifstream stream(path, std::ios::binary);
        bytes = {std::istreambuf_iterator<char>(stream), {}};
        check(!bytes.empty() && bytes.size() <= 4 * 1024 * 1024, "Invalid test font");
        blob.reset(hb_blob_create_or_fail(bytes.data(), unsigned(bytes.size()),
                                          HB_MEMORY_MODE_READONLY, nullptr, nullptr));
        check(bool(blob), "Test font blob failed");
        face.reset(hb_face_create_or_fail(blob.get(), 0));
        check(bool(face), "Test font face failed");
        font.reset(hb_font_create(face.get()));
        hb_ot_font_set_funcs(font.get());
        hb_font_set_scale(font.get(), 32 * 64, 32 * 64);
    }
};
} // namespace
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Need Latin, Arabic and known-kerning test fonts");
        Font latin(argv[1]), arabic(argv[2]), kerning(argv[3]);
        const Rml::String language = "en", arabic_language = "ar";
        Rml::TextShapingContext context{language};
        const auto run = [&](std::string_view text) {
            return forge::shape_ui_font_run(latin.font.get(), text, context);
        };
        check(run({}).glyphs.empty() && run({}).advance == 0, "Empty text acquired glyphs");
        const auto ordinary = run("office");
        context.letter_spacing = 1;
        const auto spaced = run("office");
        check(ordinary.glyphs.size() < spaced.glyphs.size() && spaced.glyphs.size() == 6,
              "Tracking did not disable discretionary Latin ligatures");
        context.letter_spacing = 0;
        const auto accent = run(utf8(u8"a\u0301"));
        check(accent.glyphs.size() == 1 && accent.glyphs[0].cluster_byte == 0,
              "Combining input lost its native shaping cluster");
        const auto baseline_width = accent.advance;
        context.letter_spacing = 3;
        check(std::abs(run(utf8(u8"a\u0301")).advance - baseline_width - 3) < .001,
              "Tracking separated a combining mark from its base");
        context.letter_spacing = 0;
        context.text_direction = Rml::Style::Direction::Rtl;
        const auto reversed = run("abc");
        check(reversed.right_to_left && reversed.glyphs.size() == 3 &&
                  reversed.glyphs[0].cluster_byte == 2 && reversed.glyphs[2].cluster_byte == 0,
              "Explicit RTL did not preserve descending source clusters");
        Rml::TextShapingContext rtl{arabic_language};
        const auto joined = forge::shape_ui_font_run(arabic.font.get(), utf8(u8"سلام"), rtl);
        check(joined.right_to_left && joined.glyphs.size() < 4 && joined.advance > 0 &&
                  std::ranges::all_of(joined.glyphs, [](const auto& glyph) { return glyph.index; }),
              "Arabic shaping lost joining/ligature glyphs");
        check(std::is_sorted(
                  joined.glyphs.begin(), joined.glyphs.end(),
                  [](const auto& a, const auto& b) { return a.cluster_byte > b.cluster_byte; }),
              "RTL glyph clusters did not retain source order mapping");
        context.text_direction = Rml::Style::Direction::Ltr;
        const auto kerned = forge::shape_ui_font_run(kerning.font.get(), "ACAB", context).advance;
        context.font_kerning = Rml::Style::FontKerning::None;
        check(forge::shape_ui_font_run(kerning.font.get(), "ACAB", context).advance > kerned,
              "Kerning control did not reach native shaping");
        rejects([&] { forge::shape_ui_font_run(nullptr, "text", context); });
        rejects([&] { run(std::string(65537, 'a')); });
        rejects([&] { run(std::string(20000, 'a')); });
        context.letter_spacing = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { run("text"); });
        context.letter_spacing = 0;
        check(run("Ready").glyphs.size() == 5, "Rejected runs damaged the borrowed font");
        std::cout << "Bounded source-byte clusters, ligatures/marks, RTL/Arabic, tracking, "
                     "kerning and rejection recovery passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
