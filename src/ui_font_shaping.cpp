#include "ui_font_shaping.hpp"
#include <array>
#include <cmath>
#include <hb.h>
#include <memory>
#include <stdexcept>

namespace forge {
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
UiShapedRun shape_ui_font_run(hb_font_t* font, std::string_view text,
                              const Rml::TextShapingContext& context) {
    require(font && font != hb_font_get_empty(), "UI shaping requires an admitted sized font");
    require(text.size() <= 64 * 1024, "UI shaped text run exceeds64KiB");
    require(std::isfinite(context.letter_spacing) && std::abs(context.letter_spacing) <= 4096,
            "UI text spacing is outside the shaped profile");
    UiShapedRun result;
    if (text.empty())
        return result;
    std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(),
                                                                      hb_buffer_destroy);
    require(buffer.get() != hb_buffer_get_empty(), "UI shaping buffer allocation failed");
    hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_set_language(
        buffer.get(),
        hb_language_from_string(context.language.empty() ? "und" : context.language.c_str(), -1));
    if (context.text_direction == Rml::Style::Direction::Ltr)
        hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
    else if (context.text_direction == Rml::Style::Direction::Rtl)
        hb_buffer_set_direction(buffer.get(), HB_DIRECTION_RTL);
    hb_buffer_add_utf8(buffer.get(), text.data(), int(text.size()), 0, int(text.size()));
    hb_buffer_guess_segment_properties(buffer.get());
    std::array<hb_feature_t, 3> features{};
    unsigned feature_count = 0;
    const auto feature = [&](const char* declaration) {
        require(hb_feature_from_string(declaration, -1, &features.at(feature_count)),
                "UI shaping feature declaration failed");
        ++feature_count;
    };
    if (context.font_kerning == Rml::Style::FontKerning::None)
        feature("kern=0");
    if (context.letter_spacing != 0) {
        feature("liga=0");
        feature("clig=0");
    }
    hb_shape(font, buffer.get(), features.data(), feature_count);
    require(hb_buffer_allocation_successful(buffer.get()), "UI text shaping allocation failed");
    const auto direction = hb_buffer_get_direction(buffer.get());
    require(direction == HB_DIRECTION_LTR || direction == HB_DIRECTION_RTL,
            "UI shaping supports horizontal text runs");
    result.right_to_left = direction == HB_DIRECTION_RTL;
    unsigned count = 0, positions_count = 0;
    const auto* glyphs = hb_buffer_get_glyph_infos(buffer.get(), &count);
    const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &positions_count);
    require(count <= 16384 && count == positions_count, "UI shaped glyph count exceeds16384");
    result.glyphs.reserve(count);
    float x = 0, y = 0;
    for (unsigned i = 0; i < count; ++i) {
        const float dx = positions[i].x_advance / 64.f;
        const float dy = positions[i].y_advance / 64.f;
        const float ox = positions[i].x_offset / 64.f;
        const float oy = positions[i].y_offset / 64.f;
        require(std::abs(dx) <= 4096 && std::abs(dy) <= 4096 && std::abs(ox) <= 32768 &&
                    std::abs(oy) <= 32768 && glyphs[i].cluster < text.size(),
                "UI glyph metrics exceed the shaped profile");
        result.glyphs.push_back({glyphs[i].codepoint, glyphs[i].cluster, x + ox, y - oy});
        x += dx;
        y -= dy;
        // A combining cluster advances once; its marks retain their native offsets.
        if (i + 1 == count || glyphs[i + 1].cluster != glyphs[i].cluster)
            x += context.letter_spacing;
        require(std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 1000000 &&
                    std::abs(y) <= 1000000,
                "UI shaped text extent exceeds the profile");
    }
    result.advance = x;
    return result;
}
} // namespace forge
